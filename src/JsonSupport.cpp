#include "JsonSupport.h"

#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace umami {

namespace {

class JsonParser {
public:
	explicit JsonParser(const std::string& text)
		: fText(text) {
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool Parse(JsonValue& value, std::string& error) {
		SkipWhitespace();
		if (!ParseValue(value, error))
			return false;
		SkipWhitespace();
		if (fPosition != fText.size()) {
			error = ErrorAt("trailing content");
			return false;
		}
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

private:
	bool ParseValue(JsonValue& value, std::string& error) {
		if (fPosition >= fText.size()) {
			error = ErrorAt("expected a value");
			return false;
		}
		char character = fText[fPosition];
		if (character == '{')
			return ParseObject(value, error);
		if (character == '[')
			return ParseArray(value, error);
		if (character == '"') {
			std::string stringValue;
			if (!ParseString(stringValue, error))
				return false;
			value.SetString(std::move(stringValue));
			return true;
		}
		if (character == '-' || std::isdigit(static_cast<unsigned char>(character)))
			return ParseInteger(value, error);
		if (ConsumeLiteral("true")) {
			value.SetBoolean(true);
			return true;
		}
		if (ConsumeLiteral("false")) {
			value.SetBoolean(false);
			return true;
		}
		if (ConsumeLiteral("null")) {
			value = JsonValue();
			return true;
		}
		error = ErrorAt("unexpected character");
		return false;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseObject(JsonValue& value, std::string& error) {
		++fPosition;
		value = JsonValue(JsonValue::Type::Object);
		SkipWhitespace();
		if (Consume('}'))
			return true;
		while (true) {
			std::string name;
			if (!ParseString(name, error))
				return false;
			SkipWhitespace();
			if (!Consume(':')) {
				error = ErrorAt("expected ':' after object field name");
				return false;
			}
			SkipWhitespace();
			JsonValue member;
			if (!ParseValue(member, error))
				return false;
			if (!value.MutableObject().emplace(name, std::move(member)).second) {
				error = ErrorAt("duplicate object field '" + name + "'");
				return false;
			}
			SkipWhitespace();
			if (Consume('}'))
				return true;
			if (!Consume(',')) {
				error = ErrorAt("expected ',' or '}' in object");
				return false;
			}
			SkipWhitespace();
		}
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseArray(JsonValue& value, std::string& error) {
		++fPosition;
		value = JsonValue(JsonValue::Type::Array);
		SkipWhitespace();
		if (Consume(']'))
			return true;
		while (true) {
			JsonValue member;
			if (!ParseValue(member, error))
				return false;
			value.MutableArray().push_back(std::move(member));
			SkipWhitespace();
			if (Consume(']'))
				return true;
			if (!Consume(',')) {
				error = ErrorAt("expected ',' or ']' in array");
				return false;
			}
			SkipWhitespace();
		}
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseString(std::string& value, std::string& error) {
		if (!Consume('"')) {
			error = ErrorAt("expected a string");
			return false;
		}
		value.clear();
		while (fPosition < fText.size()) {
			unsigned char character = static_cast<unsigned char>(fText[fPosition++]);
			if (character == '"')
				return true;
			if (character < 0x20) {
				error = ErrorAt("control character in string");
				return false;
			}
			if (character != '\\') {
				value += static_cast<char>(character);
				continue;
			}
			if (fPosition >= fText.size()) {
				error = ErrorAt("unfinished string escape");
				return false;
			}
			char escaped = fText[fPosition++];
			switch (escaped) {
				case '"': value += '"'; break;
				case '\\': value += '\\'; break;
				case '/': value += '/'; break;
				case 'b': value += '\b'; break;
				case 'f': value += '\f'; break;
				case 'n': value += '\n'; break;
				case 'r': value += '\r'; break;
				case 't': value += '\t'; break;
				case 'u':
					if (!ParseUnicode(value, error))
						return false;
					break;
				default:
					error = ErrorAt("invalid string escape");
					return false;
			}
		}
		error = ErrorAt("unterminated string");
		return false;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseUnicode(std::string& value, std::string& error) {
		std::uint32_t codePoint = 0;
		if (!ParseHexQuad(codePoint)) {
			error = ErrorAt("invalid Unicode escape");
			return false;
		}
		if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
			if (fPosition + 2 > fText.size() || fText[fPosition] != '\\' || fText[fPosition + 1] != 'u') {
				error = ErrorAt("missing low Unicode surrogate");
				return false;
			}
			fPosition += 2;
			std::uint32_t lowSurrogate = 0;
			if (!ParseHexQuad(lowSurrogate) || lowSurrogate < 0xdc00 || lowSurrogate > 0xdfff) {
				error = ErrorAt("invalid low Unicode surrogate");
				return false;
			}
			codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (lowSurrogate - 0xdc00);
		} else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
			error = ErrorAt("unexpected low Unicode surrogate");
			return false;
		}
		AppendUtf8(codePoint, value);
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseHexQuad(std::uint32_t& value) {
		if (fPosition + 4 > fText.size())
			return false;
		value = 0;
		for (int index = 0; index < 4; ++index) {
			char character = fText[fPosition++];
			value <<= 4;
			if (character >= '0' && character <= '9')
				value += static_cast<std::uint32_t>(character - '0');
			else if (character >= 'a' && character <= 'f')
				value += static_cast<std::uint32_t>(character - 'a' + 10);
			else if (character >= 'A' && character <= 'F')
				value += static_cast<std::uint32_t>(character - 'A' + 10);
			else
				return false;
		}
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	void AppendUtf8(std::uint32_t codePoint, std::string& value) {
		if (codePoint <= 0x7f) {
			value += static_cast<char>(codePoint);
		} else if (codePoint <= 0x7ff) {
			value += static_cast<char>(0xc0 | (codePoint >> 6));
			value += static_cast<char>(0x80 | (codePoint & 0x3f));
		} else if (codePoint <= 0xffff) {
			value += static_cast<char>(0xe0 | (codePoint >> 12));
			value += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
			value += static_cast<char>(0x80 | (codePoint & 0x3f));
		} else {
			value += static_cast<char>(0xf0 | (codePoint >> 18));
			value += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
			value += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
			value += static_cast<char>(0x80 | (codePoint & 0x3f));
		}
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ParseInteger(JsonValue& value, std::string& error) {
		std::size_t start = fPosition;
		if (Consume('-') && fPosition >= fText.size()) {
			error = ErrorAt("incomplete number");
			return false;
		}
		if (Consume('0')) {
			if (fPosition < fText.size() && std::isdigit(static_cast<unsigned char>(fText[fPosition]))) {
				error = ErrorAt("leading zero in number");
				return false;
			}
		} else {
			std::size_t digitStart = fPosition;
			while (fPosition < fText.size() && std::isdigit(static_cast<unsigned char>(fText[fPosition])))
				++fPosition;
			if (digitStart == fPosition) {
				error = ErrorAt("invalid number");
				return false;
			}
		}
		if (fPosition < fText.size() && (fText[fPosition] == '.' || fText[fPosition] == 'e' || fText[fPosition] == 'E')) {
			error = ErrorAt("catalog numbers must be integers");
			return false;
		}
		try {
			value.SetInteger(std::stoll(fText.substr(start, fPosition - start)));
		} catch (...) {
			error = ErrorAt("integer is out of range");
			return false;
		}
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool Consume(char character) {
		if (fPosition >= fText.size() || fText[fPosition] != character)
			return false;
		++fPosition;
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	bool ConsumeLiteral(const char* literal) {
		std::size_t length = std::char_traits<char>::length(literal);
		if (fText.compare(fPosition, length, literal) != 0)
			return false;
		fPosition += length;
		return true;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	void SkipWhitespace() {
		while (fPosition < fText.size() && std::isspace(static_cast<unsigned char>(fText[fPosition])))
			++fPosition;
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	std::string ErrorAt(const std::string& message) const {
		return message + " at byte " + std::to_string(fPosition + 1);
	}
	//---------------------------------------------------------------------------------------------------------------------------------//

	const std::string& fText;
	std::size_t fPosition = 0;
};

}

JsonValue::JsonValue()
	: fType(Type::Null) {
}
//---------------------------------------------------------------------------------------------------------------------------------//

JsonValue::JsonValue(Type type)
	: fType(type) {
}
//---------------------------------------------------------------------------------------------------------------------------------//

JsonValue::Type JsonValue::ValueType() const {
	return fType;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool JsonValue::BooleanValue() const {
	return fBoolean;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::int64_t JsonValue::IntegerValue() const {
	return fInteger;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const std::string& JsonValue::StringValue() const {
	return fString;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const std::vector<JsonValue>& JsonValue::ArrayValue() const {
	return fArray;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const std::map<std::string, JsonValue>& JsonValue::ObjectValue() const {
	return fObject;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const JsonValue* JsonValue::Find(const std::string& name) const {
	auto member = fObject.find(name);
	return member == fObject.end() ? nullptr : &member->second;
}
//---------------------------------------------------------------------------------------------------------------------------------//

void JsonValue::SetBoolean(bool value) {
	fType = Type::Boolean;
	fBoolean = value;
}
//---------------------------------------------------------------------------------------------------------------------------------//

void JsonValue::SetInteger(std::int64_t value) {
	fType = Type::Integer;
	fInteger = value;
}
//---------------------------------------------------------------------------------------------------------------------------------//

void JsonValue::SetString(std::string value) {
	fType = Type::String;
	fString = std::move(value);
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::vector<JsonValue>& JsonValue::MutableArray() {
	return fArray;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::map<std::string, JsonValue>& JsonValue::MutableObject() {
	return fObject;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool LoadJsonFile(const std::filesystem::path& path, JsonValue& root, std::string& error) {
	std::ifstream input(path);
	if (!input) {
		error = "could not open '" + path.string() + "'";
		return false;
	}
	std::ostringstream contents;
	contents << input.rdbuf();
	if (input.bad()) {
		error = "could not read '" + path.string() + "'";
		return false;
	}
	std::string text = contents.str();
	JsonParser parser(text);
	if (!parser.Parse(root, error)) {
		error = "invalid JSON in '" + path.string() + "': " + error;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ReadRequiredString(const JsonValue& object, const char* field, std::string& value, std::string& error) {
	const JsonValue* member = object.Find(field);
	if (member == nullptr || member->ValueType() != JsonValue::Type::String) {
		error = "field '" + std::string(field) + "' must be a string";
		return false;
	}
	value = member->StringValue();
	if (value.empty()) {
		error = "field '" + std::string(field) + "' must not be empty";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ReadOptionalString(const JsonValue& object, const char* field, std::string& value, std::string& error) {
	const JsonValue* member = object.Find(field);
	if (member == nullptr) {
		value.clear();
		return true;
	}
	if (member->ValueType() != JsonValue::Type::String) {
		error = "field '" + std::string(field) + "' must be a string";
		return false;
	}
	value = member->StringValue();
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
