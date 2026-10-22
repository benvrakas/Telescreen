#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// A small JSON reader for yt-dlp's subtitle lists and YouTube's json3 captions. Parse errors give a null value.
struct FJson
{
	enum class EType
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	EType Type = EType::Null;
	bool Bool = false;
	double Number = 0.0;
	std::string String;
	std::vector<FJson> Array;
	std::vector<std::pair<std::string, FJson>> Object;

	static FJson Parse(const std::string& Text);

	// Member Key of an object, or a null value.
	const FJson& operator[](const std::string& Key) const;
	bool IsObject() const { return Type == EType::Object; }
	bool IsArray() const { return Type == EType::Array; }
	const std::string& Str() const { return String; }
	double Num(double Fallback = 0.0) const { return Type == EType::Number ? Number : Fallback; }
};
