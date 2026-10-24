const OWNER = "sikosis";
const COOKBOOK = "umami-cookbook";
const PANTRY = "umami-pantry";
const API = "https://api.github.com";

const state = {
	token: "",
	recipes: [],
	recipeFiles: new Map(),
	releases: [],
	items: [],
	editingOriginalId: ""
};

const elements = Object.fromEntries([...document.querySelectorAll("[id]")].map(element => [element.id, element]));

function apiHeaders(write = false) {
	const headers = { Accept: "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28" };
	if (state.token)
		headers.Authorization = `Bearer ${state.token}`;
	if (write)
		headers["Content-Type"] = "application/json";
	return headers;
}

async function github(path, options = {}) {
	const response = await fetch(`${API}${path}`, { ...options, headers: { ...apiHeaders(Boolean(options.body)), ...(options.headers || {}) } });
	if (response.status === 204)
		return null;
	const value = await response.json().catch(() => ({}));
	if (!response.ok)
		throw new Error(value.message || `GitHub returned ${response.status}`);
	return value;
}

function escapeHtml(value) {
	return String(value ?? "").replace(/[&<>'"]/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[character]);
}

function showNotice(message, error = false) {
	elements.notice.textContent = message;
	elements.notice.style.borderColor = error ? "#c66" : "#d8a57a";
	elements.notice.style.background = error ? "#fbe5e2" : "#fff4df";
	elements.notice.classList.remove("hidden");
}

function clearNotice() {
	elements.notice.classList.add("hidden");
}

function expandAssetPattern(pattern, architecture = "x86_64") {
	return new RegExp(pattern.replaceAll("{arch}", architecture));
}

function matchingRelease(recipe, releases) {
	let matcher;
	try {
		matcher = recipe.release_tag_pattern ? new RegExp(recipe.release_tag_pattern) : null;
	} catch {
		return { error: "Invalid release tag pattern" };
	}
	return releases.find(release => !release.draft
		&& Boolean(release.prerelease) === (recipe.channel === "prerelease")
		&& (!matcher || matcher.test(release.tag_name)));
}

async function inspectRecipe(recipe) {
	let releases;
	if (recipe.repo === `${OWNER}/${PANTRY}`) {
		releases = state.releases;
	} else {
		try {
			const latest = await github(`/repos/${recipe.repo}/releases/latest`);
			releases = [latest];
		} catch (error) {
			return { recipe, status: "attention", message: error.message.includes("Not Found") ? "No published release" : error.message };
		}
	}
	const release = matchingRelease(recipe, releases);
	if (!release || release.error)
		return { recipe, status: "attention", message: release?.error || "No matching published release" };
	let assetMatcher;
	try {
		assetMatcher = expandAssetPattern(recipe.asset_pattern);
	} catch {
		return { recipe, release, status: "attention", message: "Invalid asset pattern" };
	}
	const asset = release.assets.find(candidate => assetMatcher.test(candidate.name));
	if (!asset)
		return { recipe, release, status: "attention", message: "Release has no matching x86_64 package" };
	return { recipe, release, asset, status: "ready", message: "Package and recipe agree" };
}

function inferredPantryId(release) {
	const match = release.tag_name.match(/^v.+-([a-z0-9][a-z0-9._-]*)$/i);
	return match ? match[1].toLowerCase() : "";
}

async function loadInventory() {
	clearNotice();
	elements.loading.classList.remove("hidden");
	elements.inventory.innerHTML = "";
	try {
		const [catalog, recipeFiles, releases] = await Promise.all([
			github(`/repos/${OWNER}/${COOKBOOK}/contents/catalog.json`).then(file => JSON.parse(decodeBase64(file.content))),
			github(`/repos/${OWNER}/${COOKBOOK}/contents/recipes`),
			github(`/repos/${OWNER}/${PANTRY}/releases?per_page=100`)
		]);
		state.recipes = catalog.apps;
		state.recipeFiles = new Map(recipeFiles.map(file => [file.name.replace(/\.json$/, ""), file]));
		state.releases = releases;

		const inspected = await Promise.all(state.recipes.map(inspectRecipe));
		const orphanReleases = releases.filter(release => !state.recipes.some(recipe => {
			if (recipe.repo !== `${OWNER}/${PANTRY}` || !recipe.release_tag_pattern)
				return false;
			try { return new RegExp(recipe.release_tag_pattern).test(release.tag_name); } catch { return false; }
		})).map(release => ({
			status: "orphan",
			release,
			inferredId: inferredPantryId(release),
			asset: release.assets.find(asset => asset.name.endsWith(".hpkg")),
			message: "Pantry release has no Cookbook recipe"
		}));
		state.items = [...inspected, ...orphanReleases];
		render();
	} catch (error) {
		showNotice(`Could not load the kitchen: ${error.message}`, true);
	} finally {
		elements.loading.classList.add("hidden");
	}
}

function render() {
	const query = elements.searchInput.value.trim().toLowerCase();
	const filter = elements.statusFilter.value;
	const visible = state.items.filter(item => {
		const recipe = item.recipe || {};
		const haystack = [recipe.id, recipe.name, recipe.repo, recipe.package_name, item.inferredId, item.asset?.name, item.release?.tag_name].join(" ").toLowerCase();
		const filterMatches = filter === "all" || item.status === filter || (filter === "attention" && item.status === "attention");
		return filterMatches && (!query || haystack.includes(query));
	});
	elements.recipeCount.textContent = state.recipes.length;
	elements.readyCount.textContent = state.items.filter(item => item.status === "ready").length;
	elements.attentionCount.textContent = state.items.filter(item => item.status === "attention").length;
	elements.orphanCount.textContent = state.items.filter(item => item.status === "orphan").length;
	elements.inventory.innerHTML = visible.length ? visible.map(cardHtml).join("") : '<div class="empty">Nothing matches this view.</div>';
	elements.inventory.querySelectorAll("[data-edit]").forEach(button => button.addEventListener("click", () => openRecipeEditor(state.recipes.find(recipe => recipe.id === button.dataset.edit))));
	elements.inventory.querySelectorAll("[data-generate]").forEach(button => button.addEventListener("click", () => openGeneratedRecipe(state.items[Number(button.dataset.generate)])));
}

function cardHtml(item) {
	const index = state.items.indexOf(item);
	const recipe = item.recipe;
	const name = recipe?.name || item.inferredId || item.release?.tag_name || "Unknown release";
	const id = recipe?.id || item.inferredId || "no-id";
	const source = recipe ? (recipe.repo === `${OWNER}/${PANTRY}` ? "Pantry" : "Repository") : "Pantry";
	const action = recipe
		? `<button class="button button-secondary" data-edit="${escapeHtml(recipe.id)}">Edit recipe</button>`
		: `<button class="button button-primary" data-generate="${index}" ${item.inferredId && item.asset ? "" : "disabled"}>Generate recipe</button>`;
	return `<article class="app-card">
		<div class="card-top"><div><h3>${escapeHtml(name)}</h3><p class="app-id">${escapeHtml(id)}</p></div><span class="badge badge-${item.status}">${item.status === "ready" ? "Installable" : item.status === "orphan" ? "No recipe" : "Check"}</span></div>
		<div class="meta">
			<div class="meta-row"><span>Source</span><strong>${escapeHtml(source)}</strong></div>
			<div class="meta-row"><span>Release</span><strong>${escapeHtml(item.release?.tag_name || "—")}</strong></div>
			<div class="meta-row"><span>Asset</span><strong>${escapeHtml(item.asset?.name || "—")}</strong></div>
			<div class="meta-row"><span>Recipe</span><strong>${escapeHtml(recipe ? `recipes/${recipe.id}.json` : "—")}</strong></div>
		</div>
		<p class="card-message">${escapeHtml(item.message)}</p>
		<div class="card-actions">${action}${item.release?.html_url ? `<a class="button button-quiet" href="${escapeHtml(item.release.html_url)}" target="_blank" rel="noreferrer">Release ↗</a>` : ""}</div>
	</article>`;
}

function recipeFromForm() {
	const values = Object.fromEntries(new FormData(elements.recipeForm));
	const app = {
		id: values.id.trim(), name: values.name.trim(), description: values.description.trim(),
		provider: "github", repo: values.repo.trim()
	};
	if (values.release_tag_pattern.trim())
		app.release_tag_pattern = values.release_tag_pattern.trim();
	app.asset_pattern = values.asset_pattern.trim();
	app.package_name = values.package_name.trim();
	app.channel = values.channel;
	if (values.checksum_asset.trim())
		app.checksum_asset = values.checksum_asset.trim();
	return { $schema: "../schema/recipe-v1.schema.json", version: 1, app };
}

function updatePreview() {
	try { elements.recipePreview.textContent = `${JSON.stringify(recipeFromForm(), null, 2)}\n`; } catch { elements.recipePreview.textContent = ""; }
}

function fillForm(recipe = {}) {
	for (const field of elements.recipeForm.elements) {
		if (!field.name)
			continue;
		field.value = recipe[field.name] || (field.name === "repo" ? `${OWNER}/${PANTRY}` : field.name === "channel" ? "stable" : "");
	}
	updatePreview();
}

function openRecipeEditor(recipe) {
	state.editingOriginalId = recipe?.id || "";
	elements.recipeDialogTitle.textContent = recipe ? `Edit ${recipe.name}` : "New recipe";
	elements.deleteRecipeButton.classList.toggle("hidden", !recipe);
	fillForm(recipe || {});
	elements.recipeDialog.showModal();
}

function openGeneratedRecipe(item) {
	const id = item.inferredId;
	const assetBase = item.asset.name.replace(/-\d.*$/, "");
	openRecipeEditor({
		id, name: id.replace(/(^|[-_])\w/g, value => value.replace(/[-_]/, " ").toUpperCase()),
		description: `${id} for Haiku.`, repo: `${OWNER}/${PANTRY}`,
		release_tag_pattern: `^v[0-9].*-${id}$`, asset_pattern: `^${assetBase}-.*-{arch}\\.hpkg$`,
		package_name: assetBase, channel: item.release.prerelease ? "prerelease" : "stable"
	});
}

function encodeBase64(value) {
	const bytes = new TextEncoder().encode(value);
	let binary = "";
	bytes.forEach(byte => { binary += String.fromCharCode(byte); });
	return btoa(binary);
}

function decodeBase64(value) {
	const binary = atob(value.replace(/\s/g, ""));
	return new TextDecoder().decode(Uint8Array.from(binary, character => character.charCodeAt(0)));
}

function requireToken() {
	if (state.token)
		return true;
	elements.credentialsDialog.showModal();
	showNotice("Add a GitHub token before committing Cookbook changes.");
	return false;
}

async function saveRecipe(event) {
	event.preventDefault();
	if (!elements.recipeForm.reportValidity() || !requireToken())
		return;
	const recipe = recipeFromForm();
	if (state.editingOriginalId && state.editingOriginalId !== recipe.app.id) {
		showNotice("Recipe IDs cannot be renamed. Create a new recipe and delete the old one instead.", true);
		return;
	}
	const existing = state.recipeFiles.get(recipe.app.id);
	const body = { message: `${existing ? "Update" : "Add"} ${recipe.app.name} recipe`, content: encodeBase64(`${JSON.stringify(recipe, null, 2)}\n`) };
	if (existing)
		body.sha = existing.sha;
	try {
		await github(`/repos/${OWNER}/${COOKBOOK}/contents/recipes/${encodeURIComponent(recipe.app.id)}.json`, { method: "PUT", body: JSON.stringify(body) });
		elements.recipeDialog.close();
		showNotice(`${recipe.app.name} was committed. The Cookbook workflow will regenerate the catalogue shortly.`);
		setTimeout(loadInventory, 3500);
	} catch (error) {
		showNotice(`Recipe commit failed: ${error.message}`, true);
	}
}

async function deleteRecipe() {
	if (!state.editingOriginalId || !requireToken())
		return;
	const id = state.editingOriginalId;
	const file = state.recipeFiles.get(id);
	if (!file || !confirm(`Delete recipes/${id}.json from the Cookbook? This will remove it from the next generated catalogue.`))
		return;
	try {
		await github(`/repos/${OWNER}/${COOKBOOK}/contents/recipes/${encodeURIComponent(id)}.json`, { method: "DELETE", body: JSON.stringify({ message: `Remove ${id} recipe`, sha: file.sha }) });
		elements.recipeDialog.close();
		showNotice(`${id} was removed from the Cookbook.`);
		setTimeout(loadInventory, 3500);
	} catch (error) {
		showNotice(`Recipe deletion failed: ${error.message}`, true);
	}
}

function downloadRecipe() {
	const recipe = recipeFromForm();
	const blob = new Blob([`${JSON.stringify(recipe, null, 2)}\n`], { type: "application/json" });
	const link = document.createElement("a");
	link.href = URL.createObjectURL(blob);
	link.download = `${recipe.app.id || "recipe"}.json`;
	link.click();
	URL.revokeObjectURL(link.href);
}

elements.refreshButton.addEventListener("click", loadInventory);
elements.searchInput.addEventListener("input", render);
elements.statusFilter.addEventListener("change", render);
elements.newRecipeButton.addEventListener("click", () => openRecipeEditor());
elements.credentialsButton.addEventListener("click", () => elements.credentialsDialog.showModal());
elements.recipeForm.addEventListener("input", updatePreview);
elements.recipeForm.addEventListener("submit", saveRecipe);
elements.deleteRecipeButton.addEventListener("click", deleteRecipe);
elements.downloadRecipeButton.addEventListener("click", downloadRecipe);
elements.credentialsForm.addEventListener("submit", event => {
	event.preventDefault();
	state.token = elements.tokenInput.value.trim();
	elements.tokenInput.value = "";
	elements.credentialsDialog.close();
	elements.credentialsButton.textContent = state.token ? "GitHub connected" : "GitHub access";
	showNotice(state.token ? "GitHub token is active for this tab only." : "No token was entered.", !state.token);
});
elements.clearTokenButton.addEventListener("click", () => {
	state.token = "";
	elements.tokenInput.value = "";
	elements.credentialsDialog.close();
	elements.credentialsButton.textContent = "GitHub access";
	showNotice("GitHub token cleared from memory.");
});
document.querySelectorAll("[data-close]").forEach(button => button.addEventListener("click", () => document.getElementById(button.dataset.close).close()));

loadInventory();
