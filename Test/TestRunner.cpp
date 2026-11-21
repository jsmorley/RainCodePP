// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).

// Runs the Rainmeter lexer over the .ini files in Cases and compares the styles and fold
// levels it produces against the matching .styled file.
//
// Usage: LexerTests [--update] <case.ini>...
//
#include <cstdint>

#include "ILexer.h"
#include "Scintilla.h"

#include "TestDocument.h"
#include "Syntax.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace RainLexer {
extern "C" Scintilla::ILexer5* SCI_METHOD CreateLexer(const char* name);
}

namespace {

bool CheckEditing()
{
    using namespace RainLexer;
    const auto suggest = [](std::string_view text, std::string_view candidate, bool variables = false) {
        const auto terms = TermsAt(text, text.size(), variables);
        return std::find(terms.candidates.begin(), terms.candidates.end(), candidate) != terms.candidates.end();
    };
    const auto correctsTo = [](std::string_view text, std::string_view token, std::string_view canonical) {
        const auto start = text.find(token);
        const auto terms = TermsAt(text, start + token.size(), false);
        return terms.start == start && terms.end == start + token.size() && terms.canonical == canonical;
    };
    bool okay =
        suggest("StringAli", "StringAlign") &&
        suggest("Font", "FontColor") && suggest("Font", "FontFace") &&
        suggest("Font", "FontSize") && suggest("Font", "FontWeight") &&
        !suggest("StringAli", "StringAlign", true) &&
        suggest("Formula=max(si", "sin") &&
        suggest("Text=[&Battery:MaxV", "MaxValue") &&
        suggest("Text=[@MeterCaption:XW", "XW") &&
        suggest("OnUpdateAction=[!Skin:Ref", "Refresh") &&
        correctsTo("meter=String", "meter", "Meter") &&
        correctsTo("FONTCOLOR=255,255,255", "FONTCOLOR", "FontColor") &&
        correctsTo("Meter=textedit", "textedit", "TextEdit") &&
        TermsAt("Fonstize=14", 8, false).typo == "FontSize" &&
        correctsTo("OnUpdateAction=[!setoption MeterLabel StringAlign Right]", "setoption", "SetOption") &&
        correctsTo("OnUpdateAction=[!SetOption MeterLabel stringalign Right]", "stringalign", "StringAlign") &&
        TermsAt("Text=foobar", 11, false).canonical.empty() &&
        TermsAt("; !SetOption", 12, false).candidates.empty();
    auto groups = ReadVocabulary({});
    groups[0] += "ExtraSkinOption\r\n";
    groups[4] += "ExampleCommand\r\n";
    const auto overrides = WriteVocabulary(groups);
    ConfigureVocabulary(overrides);
    okay = okay && suggest("ExtraS", "ExtraSkinOption") && suggest("OnUpdateAction=[!ExampleC", "ExampleCommand") &&
        StyleLine("ExtraSkinOption=1", false)[0] == Option;
    ConfigureVocabulary({});
    if (!okay) std::fputs("FAIL vocabulary and editing contexts\n", stderr);
    return okay;
}

bool CheckRegexSubstituteEdit()
{
    using namespace RainLexer;
    TestDocument doc("[Measure]\nSubstitute=\"a+\":\"literal\"\nRegExpSubstitute=0\n");
    const auto flag = doc.Text().find("=0") + 1;
    const auto pattern = doc.Text().find("a+");
    auto* lexer = CreateLexer(LexerName);
    lexer->Lex(0, doc.Length(), 0, &doc);
    bool okay = doc.StyleAt(pattern) == Default;
    doc.ReplaceByte(flag, '1');
    lexer->Lex(flag, 1, 0, &doc);
    okay = okay && doc.StyleAt(pattern) == ScintillaStyle(RegularExpression) &&
        doc.StyleAt(doc.Text().find("literal")) == Default;
    doc.ReplaceByte(flag, '0');
    lexer->Lex(flag, 1, 0, &doc);
    okay = okay && doc.StyleAt(pattern) == Default;
    lexer->Release();
    if (!okay) std::fputs("FAIL restyling Substitute when RegExpSubstitute changes\n", stderr);
    return okay;
}


// Chunk sizes used to re-lex a document the way Scintilla styles on demand. Small values
// force boundaries into the middle of tokens and lines.
constexpr Sci_Position kChunkSizes[] = { 1, 7, 64, 997 };

bool ReadFile(const std::string& path, std::string& contents)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return false;
	}

	std::ostringstream buffer;
	buffer << file.rdbuf();
	contents = buffer.str();
	return true;
}

bool WriteFile(const std::string& path, const std::string& contents)
{
	std::ofstream file(path, std::ios::binary);
	if (!file)
	{
		return false;
	}

	file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return file.good();
}

// Renders the styled text as runs prefixed with their style number. Line endings are
// spelled out so that a difference in how they are styled is visible, and every output
// line restarts with an explicit style so a change on one line does not shift the ones
// after it.
std::string FormatStyles(const TestDocument& doc)
{
	const std::string& text = doc.Text();
	const std::vector<char>& styles = doc.Styles();

	std::string out;
	int current = -1;

	for (size_t i = 0; i < text.size(); ++i)
	{
		const int style = static_cast<unsigned char>(styles[i]);
		if (style != current)
		{
			out += '{';
			out += std::to_string(style);
			out += '}';
			current = style;
		}

		switch (text[i])
		{
		case '\r':
			out += "\\r";
			break;

		case '\n':
			out += "\\n\n";
			current = -1;
			break;

		case '{':
			out += "{{";
			break;

		default:
			out += text[i];
			break;
		}
	}

	if (!out.empty() && out.back() != '\n')
	{
		out += '\n';
	}

	return out;
}

std::string FormatFolds(const TestDocument& doc)
{
	std::string out = "--- folds ---\n";

	for (Sci_Position line = 0; line < doc.LineCount(); ++line)
	{
		const int level = doc.Levels()[static_cast<size_t>(line)];

		char buffer[64];
		snprintf(
			buffer, sizeof(buffer), "%lld: 0x%04X%s\n",
			static_cast<long long>(line), static_cast<unsigned>(level),
			((level & SC_FOLDLEVELHEADERFLAG) != 0) ? " header" : "");
		out += buffer;
	}

	return out;
}

// Styles written past the end of the document are discarded by Scintilla rather than
// reported, so track them here: they are invisible in the editor but they are still the
// lexer reaching outside the buffer.
std::string FormatOutOfRange(const TestDocument& doc)
{
	char buffer[128];
	snprintf(
		buffer, sizeof(buffer), "--- out of range ---\nstyles: %d\nlevels: %d\n",
		doc.StylesOutOfRange(), doc.LevelsOutOfRange());
	return buffer;
}

void LexWhole(Scintilla::ILexer5* lexer, TestDocument& doc)
{
	const Sci_Position length = doc.Length();
	if (length == 0)
	{
		return;
	}

	lexer->Lex(0, length, 0, &doc);
	lexer->Fold(0, length, 0, &doc);
}

// Styles the document in pieces the way Scintilla does when it only needs part of it
// painted, always resuming from the start of the line that styling stopped inside.
void LexIncremental(Scintilla::ILexer5* lexer, TestDocument& doc, Sci_Position chunk)
{
	const Sci_Position length = doc.Length();
	Sci_Position target = 0;

	while (target < length)
	{
		const Sci_Position start = target;
		target = std::min(target + chunk, length);
		lexer->Lex(start, target - start, 0, &doc);
		lexer->Fold(start, target - start, 0, &doc);
	}
}

std::string ExpectedPathFor(const std::string& casePath)
{
	const size_t dot = casePath.find_last_of('.');
	return casePath.substr(0, dot) + ".styled";
}

// Prints the first line that differs, which is nearly always enough to see what changed.
void ReportDifference(const std::string& expected, const std::string& actual)
{
	std::istringstream expectedLines(expected);
	std::istringstream actualLines(actual);

	std::string expectedLine;
	std::string actualLine;
	int line = 1;

	while (true)
	{
		const bool haveExpected = static_cast<bool>(std::getline(expectedLines, expectedLine));
		const bool haveActual = static_cast<bool>(std::getline(actualLines, actualLine));

		if (!haveExpected && !haveActual)
		{
			return;
		}

		if (!haveExpected || !haveActual || expectedLine != actualLine)
		{
			printf("    first difference on line %d\n", line);
			printf("      expected: %s\n", haveExpected ? expectedLine.c_str() : "<end of file>");
			printf("      actual:   %s\n", haveActual ? actualLine.c_str() : "<end of file>");
			return;
		}

		++line;
	}
}

}  // namespace

int main(int argc, char* argv[])
{
	int firstCase = 1;
	const bool update = argc >= 2 && std::string_view(argv[1]) == "--update";
	if (update) ++firstCase;
	if (firstCase >= argc)
	{
		fprintf(stderr, "No test cases given\n");
		return 2;
	}

	Scintilla::ILexer5* lexer = RainLexer::CreateLexer(RainLexer::LexerName);
	if (lexer == nullptr)
	{
		fprintf(stderr, "CreateLexer returned null\n");
		return 2;
	}

	int failures = (CheckEditing() ? 0 : 1) + (CheckRegexSubstituteEdit() ? 0 : 1);
	int passes = 0;

	for (int argi = firstCase; argi < argc; ++argi)
	{
		const std::string casePath = argv[argi];

		std::string text;
		if (!ReadFile(casePath, text))
		{
			printf("FAIL %s\n    unable to read case\n", casePath.c_str());
			++failures;
			continue;
		}

		TestDocument doc(text);
		LexWhole(lexer, doc);

		const std::string actual =
			FormatStyles(doc) + FormatFolds(doc) + FormatOutOfRange(doc);

		bool caseFailed = false;

		// Styling the document in pieces has to land on the same result as styling it in
		// one go, because Scintilla decides where those boundaries fall.
		for (const Sci_Position chunk : kChunkSizes)
		{
			TestDocument piecewise(text);
			LexIncremental(lexer, piecewise, chunk);

			if (piecewise.Styles() != doc.Styles())
			{
				printf(
					"FAIL %s\n    styling in chunks of %lld differs from styling it all at once\n",
					casePath.c_str(), static_cast<long long>(chunk));
				ReportDifference(FormatStyles(doc), FormatStyles(piecewise));
				caseFailed = true;
				break;
			}

			if (piecewise.Levels() != doc.Levels())
			{
				printf(
					"FAIL %s\n    folding in chunks of %lld differs from folding it all at once\n",
					casePath.c_str(), static_cast<long long>(chunk));
				ReportDifference(FormatFolds(doc), FormatFolds(piecewise));
				caseFailed = true;
				break;
			}
		}

		const std::string expectedPath = ExpectedPathFor(casePath);

		if (update)
		{
			if (!WriteFile(expectedPath, actual))
			{
				printf("FAIL %s\n    unable to write %s\n", casePath.c_str(), expectedPath.c_str());
				++failures;
				continue;
			}

			printf("UPDATED %s\n", expectedPath.c_str());
		}
		else
		{
			std::string expected;
			if (!ReadFile(expectedPath, expected))
			{
				printf("FAIL %s\n    missing %s, run with --update to create it\n",
					casePath.c_str(), expectedPath.c_str());
				++failures;
				continue;
			}

			if (expected != actual)
			{
				printf("FAIL %s\n    output does not match %s\n", casePath.c_str(), expectedPath.c_str());
				ReportDifference(expected, actual);
				caseFailed = true;
			}
		}

		if (caseFailed)
		{
			++failures;
		}
		else if (!update)
		{
			printf("PASS %s\n", casePath.c_str());
			++passes;
		}
	}

	lexer->Release();

	if (update)
	{
		return 0;
	}

	printf("\n%d passed, %d failed\n", passes, failures);
	return (failures == 0) ? 0 : 1;
}
