// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exceptions.h"
#include "GeneratedCodeVersion.h"
#include "ParserHelper.h"
#include "Containers/UnrealString.h"

class FToken;

enum class EPointerMemberBehavior
{
	Disallow,
	AllowSilently,
	AllowAndLog,
};

using FMetaData = TMap<FName, FString>;

/////////////////////////////////////////////////////
// UHTConfig

struct FUHTConfig
{
	static const FUHTConfig& Get();

	// Types that have been renamed, treat the old deprecated name as the new name for code generation
	TMap<FString, FString> TypeRedirectMap;

	// Special parsed struct names that do not require a prefix
	TArray<FString> StructsWithNoPrefix;

	// Special parsed struct names that have a 'T' prefix
	TArray<FString> StructsWithTPrefix;

	// Mapping from 'human-readable' macro substring to # of parameters for delegate declarations
	// Index 0 is 1 parameter, Index 1 is 2, etc...
	TArray<FString> DelegateParameterCountStrings;

	// Default version of generated code. Defaults to oldest possible, unless specified otherwise in config.
	EGeneratedCodeVersion DefaultGeneratedCodeVersion = EGeneratedCodeVersion::V1;

	EPointerMemberBehavior NativePointerMemberBehavior = EPointerMemberBehavior::AllowSilently;

	EPointerMemberBehavior ObjectPtrMemberBehavior = EPointerMemberBehavior::AllowSilently;

private:
	FUHTConfig();
};

/////////////////////////////////////////////////////
// FBaseParser

enum class ESymbolParseOption
{
	Normal,
	CloseTemplateBracket
};

// A specifier with optional value
struct FPropertySpecifier
{
public:
	explicit FPropertySpecifier(FString&& InKey)
		: Key(MoveTemp(InKey))
	{
	}

	explicit FPropertySpecifier(const FString& InKey)
		: Key(InKey)
	{
	}

	FString Key;
	TArray<FString> Values;

	FString ConvertToString() const;
};

//
// Base class of header parsers.
//

class FBaseParser 
	: public FUHTExceptionContext
{
protected:
	FBaseParser(FUnrealSourceFile& InSourceFile);
	virtual ~FBaseParser() = default;

	// UHTConfig data
	const FUHTConfig& UHTConfig;

public:
	// Source being parsed
	FUnrealSourceFile& SourceFile;

	// Input text.
	const TCHAR* Input;

	// Length of input text.
	int32 InputLen;

	// Current position in text.
	int32 InputPos;

	// Current line in text.
	int32 InputLine;

	// Position previous to last GetChar() call.
	int32 PrevPos;

	// Line previous to last GetChar() call.
	int32 PrevLine;

	// Previous comment parsed by GetChar() call.
	FString PrevComment;

	// Number of statements parsed.
	int32 StatementsParsed;

	// Total number of lines parsed.
	int32 LinesParsed;

	virtual FString GetFilename() const override;

	virtual int32 GetLineNumber() const override
	{
		return InputLine;
	};

	void ResetParser(const TCHAR* SourceBuffer, int32 StartingLineNumber = 1);

	// Low-level parsing functions.
	TCHAR GetChar( bool Literal = false );
	TCHAR PeekChar();
	TCHAR GetLeadingChar();
	void UngetChar();

	/**
	 * Tests if a character is an end-of-line character.
	 *
	 * @param	c	The character to test.
	 *
	 * @return	true if c is an end-of-line character, false otherwise.
	 */
	static bool IsEOL( TCHAR c );

	/**
	 * Tests if a character is a whitespace character.
	 *
	 * @param	c	The character to test.
	 *
	 * @return	true if c is an whitespace character, false otherwise.
	 */
	static bool IsWhitespace( TCHAR c );

	/**
	 * Gets the next token from the input stream, advancing the variables which keep track of the current input position and line.
	 *
	 * @param	Token						receives the value of the parsed text; if Token is pre-initialized, special logic is performed
	 *										to attempt to evaluated Token in the context of that type.  Useful for distinguishing between ambigous symbols
	 *										like enum tags.
	 * @param	NoConsts					specify true to indicate that tokens representing literal const values are not allowed.
	 * @param	ParseTemplateClosingBracket	specify true to treat >> as two template closing brackets instead of shift operator.
	 *
	 * @return	true if a token was successfully processed, false otherwise.
	 */
	bool GetToken( FToken& Token, bool bNoConsts = false, ESymbolParseOption bParseTemplateClosingBracket = ESymbolParseOption::Normal );

	/**
	 * Put all text from the current position up to either EOL or the StopToken
	 * into Token.  Advances the compiler's current position.
	 *
	 * @param	Token	[out] will contain the text that was parsed
	 * @param	StopChar	stop processing when this character is reached
	 *
	 * @return	true if a token was parsed
	 */
	bool GetRawToken( FToken& Token, TCHAR StopChar = TCHAR('\n') );

	// Doesn't quit if StopChar is found inside a double-quoted string, but does not support quote escapes
	bool GetRawTokenRespectingQuotes( FToken& Token, TCHAR StopChar = TCHAR('\n') );

	void UngetToken( const FToken& Token );
	void UngetToken(int32 StartLine, int32 StartPos);
	bool GetIdentifier( FToken& Token, bool bNoConsts = false );
	bool GetSymbol( FToken& Token );

	// Modify token to fix redirected types if needed
	void RedirectTypeIdentifier(FToken& Token) const;

	/**
	 * Get an int constant
	 * @return true on success, otherwise false.
	 */
	bool GetConstInt(int32& Result, const TCHAR* Tag = NULL);
	bool GetConstInt64(int64& Result, const TCHAR* Tag = NULL);

	// Matching predefined text.
	bool MatchIdentifier( const TCHAR* Match, ESearchCase::Type SearchCase);
	bool MatchConstInt( const TCHAR* Match );
	bool MatchAnyConstInt();
	bool PeekIdentifier( const TCHAR* Match, ESearchCase::Type SearchCase);
	bool MatchSymbol( const TCHAR Match, ESymbolParseOption bParseTemplateClosingBracket = ESymbolParseOption::Normal );
	bool MatchSymbol(const TCHAR* Match, ESymbolParseOption bParseTemplateClosingBracket = ESymbolParseOption::Normal);
	void MatchSemi();
	bool PeekSymbol( const TCHAR Match );

	// Requiring predefined text.
	void RequireIdentifier( const TCHAR* Match, ESearchCase::Type SearchCase, const TCHAR* Tag );
	void RequireSymbol( const TCHAR Match, const TCHAR* Tag, ESymbolParseOption bParseTemplateClosingBracket = ESymbolParseOption::Normal );
	void RequireSymbol(const TCHAR Match, TFunctionRef<FString()> TagGetter, ESymbolParseOption bParseTemplateClosingBracket = ESymbolParseOption::Normal);
	void RequireConstInt( const TCHAR* Match, const TCHAR* Tag );
	void RequireAnyConstInt( const TCHAR* Tag );

	/** Clears out the stored comment. */
	void ClearComment();

	// Reads a new-style value
	//@TODO: UCREMOVAL: Needs a better name
	FString ReadNewStyleValue(const TCHAR* TypeOfSpecifier);

	// Reads ['(' Value [',' Value]* ')'] and places each value into the Items array
	bool ReadOptionalCommaSeparatedListInParens(TArray<FString>& Items, const TCHAR* TypeOfSpecifier);

	//////////////
	// Complicated* parsing code that needs to be shared between the preparser and the parser
	// (* i.e., doesn't really belong in the base parser)

	// Expecting Name | (MODULE_API Name)
	//  Places Name into DeclaredName
	//  Places MODULE_API (where MODULE varies) into RequiredAPIMacroIfPresent
	// FailureMessage is printed out if the expectation is broken.
	void ParseNameWithPotentialAPIMacroPrefix(FString& DeclaredName, FString& RequiredAPIMacroIfPresent, const TCHAR* FailureMessage);

	// Reads a set of specifiers (with optional values) inside the () of a new-style metadata macro like UPROPERTY or UFUNCTION
	void ReadSpecifierSetInsideMacro(TArray<FPropertySpecifier>& SpecifiersFound, const TCHAR* TypeOfSpecifier, TMap<FName, FString>& MetaData);

	// Validates and inserts one key-value pair into the meta data map
	void InsertMetaDataPair(TMap<FName, FString>& MetaData, FString InKey, FString InValue);

	// Validates and inserts one key-value pair into the meta data map
	void InsertMetaDataPair(TMap<FName, FString>& MetaData, FName InKey, FString InValue);

	/**
	 * Parse class/struct inheritance.
	 *
	 * @param What				The name of the statement we are parsing.  (i.e. 'class')
	 * @param InLambda			Function to call for every parent.  Must be in the form of
	 *							Lambda(const TCHAR* Identifier, bool bSuperClass)
	 */
	template <typename Lambda>
	void ParseInheritance(const TCHAR* What, Lambda&& InLambda);

	//////////////

	// Initialize the metadata keywords prior to parsing
	static void InitMetadataKeywords();
};

template <typename Lambda>
void FBaseParser::ParseInheritance(const TCHAR* What, Lambda&& InLambda)
{

	if (!MatchSymbol(TEXT(':')))
	{
		return;
	}

	// Process the super class 
	{
		FToken Token;
		RequireIdentifier(TEXT("public"), ESearchCase::CaseSensitive, TEXT("inheritance"));
		if (!GetIdentifier(Token))
		{
			FUHTException::Throwf(*this, TEXT("Missing %s name"), What);
		}
		RedirectTypeIdentifier(Token);
		InLambda(Token.Identifier, true);
	}

	// Handle additional inherited interface classes
	while (MatchSymbol(TEXT(',')))
	{
		RequireIdentifier(TEXT("public"), ESearchCase::CaseSensitive, TEXT("Interface inheritance must be public"));

		FString InterfaceName;

		for (;;)
		{
			FToken Token;
			if (!GetIdentifier(Token, true))
			{
				FUHTException::Throwf(*this, TEXT("Failed to get interface class identifier"));
			}

			InterfaceName += Token.Identifier;

			// Handle templated native classes
			if (MatchSymbol(TEXT('<')))
			{
				InterfaceName += TEXT('<');

				int32 NestedScopes = 1;
				while (NestedScopes)
				{
					if (!GetToken(Token))
					{
						FUHTException::Throwf(*this, TEXT("Unexpected end of file"));
					}

					if (Token.TokenType == TOKEN_Symbol)
					{
						if (Token.Matches(TEXT('<')))
						{
							++NestedScopes;
						}
						else if (Token.Matches(TEXT('>')))
						{
							--NestedScopes;
						}
					}

					InterfaceName += Token.Identifier;
				}
			}

			// Handle scoped native classes
			if (MatchSymbol(TEXT("::")))
			{
				InterfaceName += TEXT("::");

				// Keep reading nested identifiers
				continue;
			}

			break;
		}
		InLambda(*InterfaceName, false);
	}
}
