#ifndef UMAMI_JSON_SUPPORT_H
#define UMAMI_JSON_SUPPORT_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace umami {

class JsonValue {
public:
	enum class Type {
		Null,
		Boolean,
		Integer,
		String,
		Array,
		Object
	};

	JsonValue();
	explicit JsonValue(Type type);

	Type ValueType() const;
	bool BooleanValue() const;
	std::int64_t IntegerValue() const;
	const std::string& StringValue() const;
	const std::vector<JsonValue>& ArrayValue() const;
	const std::map<std::string, JsonValue>& ObjectValue() const;
	const JsonValue* Find(const std::string& name) const;

	void SetBoolean(bool value);
	void SetInteger(std::int64_t value);
	void SetString(std::string value);
	std::vector<JsonValue>& MutableArray();
	std::map<std::string, JsonValue>& MutableObject();

private:
	Type fType;
	bool fBoolean = false;
	std::int64_t fInteger = 0;
	std::string fString;
	std::vector<JsonValue> fArray;
	std::map<std::string, JsonValue> fObject;
};

bool LoadJsonFile(const std::filesystem::path& path, JsonValue& root, std::string& error);
bool ReadRequiredString(const JsonValue& object, const char* field, std::string& value, std::string& error);
bool ReadOptionalString(const JsonValue& object, const char* field, std::string& value, std::string& error);

}

#endif
