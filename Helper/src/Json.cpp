#include "Json.h"

#include <cstdlib>

namespace
{
	const FJson NullJson;

	struct FParser
	{
		const char* At;
		const char* End;
		int Depth = 0;
		bool Ok = true;

		void SkipSpace()
		{
			while (At < End && (*At == ' ' || *At == '\t' || *At == '\n' || *At == '\r')) ++At;
		}

		bool Expect(const char* Word)
		{
			for (const char* W = Word; *W; ++W)
			{
				if (At >= End || *At != *W) return Ok = false;
				++At;
			}
			return true;
		}

		static void AppendUtf8(std::string& Out, unsigned Code)
		{
			if (Code < 0x80) Out += char(Code);
			else if (Code < 0x800) { Out += char(0xC0 | (Code >> 6)); Out += char(0x80 | (Code & 0x3F)); }
			else if (Code < 0x10000) { Out += char(0xE0 | (Code >> 12)); Out += char(0x80 | ((Code >> 6) & 0x3F)); Out += char(0x80 | (Code & 0x3F)); }
			else { Out += char(0xF0 | (Code >> 18)); Out += char(0x80 | ((Code >> 12) & 0x3F)); Out += char(0x80 | ((Code >> 6) & 0x3F)); Out += char(0x80 | (Code & 0x3F)); }
		}

		unsigned Hex4()
		{
			if (End - At < 4) { Ok = false; return 0; }
			unsigned Value = 0;
			for (int I = 0; I < 4; ++I)
			{
				const char C = *At++;
				Value <<= 4;
				if (C >= '0' && C <= '9') Value |= unsigned(C - '0');
				else if (C >= 'a' && C <= 'f') Value |= unsigned(C - 'a' + 10);
				else if (C >= 'A' && C <= 'F') Value |= unsigned(C - 'A' + 10);
				else { Ok = false; return 0; }
			}
			return Value;
		}

		std::string ParseString()
		{
			std::string Out;
			++At;
			while (At < End && *At != '"')
			{
				if (*At != '\\')
				{
					Out += *At++;
					continue;
				}
				if (++At >= End) break;
				const char C = *At++;
				switch (C)
				{
				case 'n': Out += '\n'; break;
				case 't': Out += '\t'; break;
				case 'r': Out += '\r'; break;
				case 'b': Out += '\b'; break;
				case 'f': Out += '\f'; break;
				case 'u':
				{
					unsigned Code = Hex4();
					if (Code >= 0xD800 && Code < 0xDC00 && End - At >= 6 && At[0] == '\\' && At[1] == 'u')
					{
						At += 2;
						const unsigned Low = Hex4();
						Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
					}
					AppendUtf8(Out, Code);
					break;
				}
				default: Out += C; break;
				}
			}
			if (At >= End) Ok = false;
			else ++At;
			return Out;
		}

		FJson ParseValue()
		{
			FJson Value;
			SkipSpace();
			if (At >= End || ++Depth > 64)
			{
				Ok = false;
				return Value;
			}
			const char C = *At;
			if (C == '{')
			{
				Value.Type = FJson::EType::Object;
				++At;
				SkipSpace();
				if (At < End && *At == '}') { ++At; --Depth; return Value; }
				while (Ok)
				{
					SkipSpace();
					if (At >= End || *At != '"') { Ok = false; break; }
					std::string Key = ParseString();
					SkipSpace();
					if (At >= End || *At != ':') { Ok = false; break; }
					++At;
					Value.Object.emplace_back(std::move(Key), ParseValue());
					SkipSpace();
					if (At < End && *At == ',') { ++At; continue; }
					if (At < End && *At == '}') { ++At; break; }
					Ok = false;
				}
			}
			else if (C == '[')
			{
				Value.Type = FJson::EType::Array;
				++At;
				SkipSpace();
				if (At < End && *At == ']') { ++At; --Depth; return Value; }
				while (Ok)
				{
					Value.Array.push_back(ParseValue());
					SkipSpace();
					if (At < End && *At == ',') { ++At; continue; }
					if (At < End && *At == ']') { ++At; break; }
					Ok = false;
				}
			}
			else if (C == '"')
			{
				Value.Type = FJson::EType::String;
				Value.String = ParseString();
			}
			else if (C == 't') { Expect("true"); Value.Type = FJson::EType::Bool; Value.Bool = true; }
			else if (C == 'f') { Expect("false"); Value.Type = FJson::EType::Bool; }
			else if (C == 'n') { Expect("null"); }
			else
			{
				char* NumberEnd = nullptr;
				Value.Number = std::strtod(At, &NumberEnd);
				if (NumberEnd == At || NumberEnd > End) Ok = false;
				else { Value.Type = FJson::EType::Number; At = NumberEnd; }
			}
			--Depth;
			return Value;
		}
	};
}

FJson FJson::Parse(const std::string& Text)
{
	FParser Parser{Text.data(), Text.data() + Text.size()};
	FJson Value = Parser.ParseValue();
	return Parser.Ok ? Value : FJson();
}

const FJson& FJson::operator[](const std::string& Key) const
{
	for (const auto& Pair : Object)
	{
		if (Pair.first == Key) return Pair.second;
	}
	return NullJson;
}
