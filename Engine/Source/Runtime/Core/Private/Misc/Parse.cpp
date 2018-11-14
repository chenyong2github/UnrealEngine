// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Misc/Parse.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "UObject/NameTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Set.h"
#include "Internationalization/Text.h"
#include "Misc/Guid.h"
#include "Misc/OutputDeviceNull.h"
#include "HAL/IConsoleManager.h"
#include "Containers/LazyPrintf.h"

#if !UE_BUILD_SHIPPING 
/**
 * Needed for the console command "DumpConsoleCommands"
 * How it works:
 *   - GConsoleCommandLibrary is set to point at a local instance of ConsoleCommandLibrary
 *   - a dummy command search is triggered which gathers all commands in a hashed set
 *   - sort all gathered commands in human friendly way
 *   - log all commands
 *   - GConsoleCommandLibrary is set 0
 */
class ConsoleCommandLibrary
{
public:
	ConsoleCommandLibrary(const FString& InPattern);

	~ConsoleCommandLibrary();

	void OnParseCommand(const TCHAR* Match)
	{
		// -1 to not take the "*" after the pattern into account
		if(FCString::Strnicmp(Match, *Pattern, Pattern.Len() - 1) == 0)
		{
			KnownNames.Add(Match);
		}
	}

	const FString&		Pattern;
	TSet<FString>		KnownNames;
};

// 0 if gathering of names is deactivated
ConsoleCommandLibrary* GConsoleCommandLibrary;

ConsoleCommandLibrary::ConsoleCommandLibrary(const FString& InPattern) :Pattern(InPattern)
{
	// activate name gathering
	GConsoleCommandLibrary = this;
}

ConsoleCommandLibrary::~ConsoleCommandLibrary()
{
	// deactivate name gathering
	GConsoleCommandLibrary = 0;
}



class FConsoleVariableDumpVisitor 
{
public:
	// @param Name must not be 0
	// @param " must not be 0
	static void OnConsoleVariable(const TCHAR *Name, IConsoleObject* CVar,TSet<FString>& Sink)
	{
		if(CVar->TestFlags(ECVF_Unregistered))
		{
			return;
		}

		Sink.Add(Name);
	}
};

void ConsoleCommandLibrary_DumpLibrary(UWorld* InWorld, FExec& SubSystem, const FString& Pattern, FOutputDevice& Ar)
{
	ConsoleCommandLibrary LocalConsoleCommandLibrary(Pattern);

	FOutputDeviceNull Null;

	bool bExecuted = SubSystem.Exec( InWorld, *Pattern, Null);

	{
		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
			FConsoleObjectVisitor::CreateStatic< TSet<FString>& >(
			&FConsoleVariableDumpVisitor::OnConsoleVariable,
			LocalConsoleCommandLibrary.KnownNames ) );
	}

	LocalConsoleCommandLibrary.KnownNames.Sort( TLess<FString>() );

	for(TSet<FString>::TConstIterator It(LocalConsoleCommandLibrary.KnownNames); It; ++It)
	{
		const FString Name = *It;

		Ar.Logf(TEXT("%s"), *Name);
	}
	Ar.Logf(TEXT(""));

	// the pattern (e.g. Motion*) should not really trigger the execution
	if(bExecuted)
	{
		Ar.Logf(TEXT("ERROR: The function was supposed to only find matching commands but not have any side effect."));
		Ar.Logf(TEXT("However Exec() returned true which means we either executed a command or the command parsing returned true where it shouldn't."));
	}
}

void ConsoleCommandLibrary_DumpLibraryHTML(UWorld* InWorld, FExec& SubSystem, const FString& OutPath)
{
	const FString& Pattern(TEXT("*"));
	ConsoleCommandLibrary LocalConsoleCommandLibrary(Pattern);

	FOutputDeviceNull Null;

	bool bExecuted = SubSystem.Exec( InWorld, *LocalConsoleCommandLibrary.Pattern, Null);

	{
		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
			FConsoleObjectVisitor::CreateStatic< TSet<FString>& >(
			&FConsoleVariableDumpVisitor::OnConsoleVariable,
			LocalConsoleCommandLibrary.KnownNames ) );
	}

	LocalConsoleCommandLibrary.KnownNames.Sort( TLess<FString>() );

	FString TemplateFilename = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("../../Documentation/Extras"), TEXT("ConsoleHelpTemplate.html"));
	FString TemplateFile;
	if(FFileHelper::LoadFileToString(TemplateFile, *TemplateFilename, FFileHelper::EHashOptions::EnableVerify | FFileHelper::EHashOptions::ErrorMissingHash) )
	{
		// todo: do we need to create the directory?
		FArchive* File = IFileManager::Get().CreateDebugFileWriter(*OutPath);

		if(File)
		{
			FLazyPrintf LazyPrintf(*TemplateFile);

			// title
			LazyPrintf.PushParam(TEXT("UE4 Console Variables and Commands"));
			// headline
			LazyPrintf.PushParam(TEXT("Unreal Engine 4 Console Variables and Commands"));
			// generated by
			LazyPrintf.PushParam(TEXT("Unreal Engine 4 console command 'Help'"));
			// version
			LazyPrintf.PushParam(TEXT("0.95"));
			// date
			LazyPrintf.PushParam(*FDateTime::Now().ToString());

			FString AllData;

			for(TSet<FString>::TConstIterator It(LocalConsoleCommandLibrary.KnownNames); It; ++It)
			{
				const FString& Name = *It;

				auto Element = IConsoleManager::Get().FindConsoleObject(*Name);

				if (Element)
				{
					// console command or variable

					FString Help = Element->GetHelp();

					Help = Help.ReplaceCharWithEscapedChar();

					const TCHAR* ElementType = TEXT("Unknown");

					if(Element->AsVariable())
					{
						ElementType = TEXT("Var"); 
					}
					else if(Element->AsCommand())
					{
						ElementType = TEXT("Cmd"); 
					}

					//{name: "r.SetRes", help:"To change the screen/window resolution."},
					FString DataLine = FString::Printf(TEXT("{name: \"%s\", help:\"%s\", type:\"%s\"},\r\n"), *Name, *Help, ElementType);

					AllData += DataLine;
				}
				else
				{
					// Exec command (better we change them to use the new method as it has better help and is more convenient to use)
					//{name: "", help:"To change the screen/window resolution."},
					FString DataLine = FString::Printf(TEXT("{name: \"%s\", help:\"Sorry: Exec commands have no help\", type:\"Exec\"},\r\n"), *Name);

					AllData += DataLine;
				}
			}

			LazyPrintf.PushParam(*AllData);

			FTCHARToUTF8 UTF8Help(*LazyPrintf.GetResultString());
			File->Serialize((ANSICHAR*)UTF8Help.Get(), UTF8Help.Length());

			delete File;
			File = 0;
		}
	}

/*
	// the pattern (e.g. Motion*) should not really trigger the execution
	if(bExecuted)
	{
		Ar.Logf(TEXT("ERROR: The function was supposed to only find matching commands but not have any side effect."));
		Ar.Logf(TEXT("However Exec() returned true which means we either executed a command or the command parsing returned true where it shouldn't."));
	}
*/
}
#endif // UE_BUILD_SHIPPING

//
// Get a string from a text string.
//
bool FParse::Value(
	const TCHAR*	Stream,
	const TCHAR*	Match,
	TCHAR*			Value,
	int32			MaxLen,
	bool			bShouldStopOnSeparator
)
{
	bool bSuccess = false;
	int32 MatchLen = FCString::Strlen(Match);

	for (const TCHAR* Found = FCString::Strifind(Stream, Match, true); Found != nullptr; Found = FCString::Strifind(Found + MatchLen, Match, true))
	{
		const TCHAR* Start = Found + MatchLen;

		// Check for quoted arguments' string with spaces
		// -Option="Value1 Value2"
		//         ^~~~Start
		bool bArgumentsQuoted = *Start == '"';

		if (bArgumentsQuoted)
		{
			// Skip quote character if only params were quoted.
			int32 QuoteCharactersToSkip = 1;
			FCString::Strncpy(Value, Start + QuoteCharactersToSkip, MaxLen);

			Value[MaxLen-1]=0;
			TCHAR* Temp = FCString::Strstr( Value, TEXT("\x22") );
			if (Temp != nullptr)
			{
				*Temp = 0;
			}
		}
		else
		{
			// Skip initial whitespace
			Start += FCString::Strspn(Start, TEXT(" \r\n\t"));

			// Non-quoted string without spaces.
			FCString::Strncpy( Value, Start, MaxLen );
			Value[MaxLen-1]=0;
			TCHAR* Temp;
			Temp = FCString::Strstr( Value, TEXT(" ")  ); if( Temp ) *Temp=0;
			Temp = FCString::Strstr( Value, TEXT("\r") ); if( Temp ) *Temp=0;
			Temp = FCString::Strstr( Value, TEXT("\n") ); if( Temp ) *Temp=0;
			Temp = FCString::Strstr( Value, TEXT("\t") ); if( Temp ) *Temp=0;
			if (bShouldStopOnSeparator)
			{
				Temp = FCString::Strstr( Value, TEXT(",")  ); if( Temp ) *Temp=0;
				Temp = FCString::Strstr( Value, TEXT(")")  ); if( Temp ) *Temp=0;
			}
		}

		bSuccess = true;
		break;
	}

	return bSuccess;
}

//
// Checks if a command-line parameter exists in the stream.
//
bool FParse::Param( const TCHAR* Stream, const TCHAR* Param )
{
	const TCHAR* Start = Stream;
	if( *Stream )
	{
		while( (Start=FCString::Strifind(Start,Param,true)) != NULL )
		{
			if( Start>Stream && (Start[-1]=='-' || Start[-1]=='/') && 
				(Stream > (Start - 2) || FChar::IsWhitespace(Start[-2]))) // Reject if the character before '-' or '/' is not a whitespace
			{
				const TCHAR* End = Start + FCString::Strlen(Param);
				if ( End == NULL || *End == 0 || FChar::IsWhitespace(*End) )
				{
					return true;
				}
			}

			Start++;
		}
	}
	return false;
}

// 
// Parse a string.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, FString& Value, bool bShouldStopOnSeparator )
{
	if (!Stream)
	{
		return false;
	}

	int32 StreamLen = FCString::Strlen(Stream);
	if (StreamLen < 4096)
	{
		TCHAR Temp[4096]=TEXT("");
		if (FParse::Value(Stream, Match, Temp, ARRAY_COUNT(Temp), bShouldStopOnSeparator))
		{
			Value = Temp;
			return true;
		}
	}
	else
	{
		FString TempValue;
		TArray<TCHAR>& ValueCharArray = TempValue.GetCharArray();
		ValueCharArray.AddUninitialized(StreamLen + 1);
		if( FParse::Value( Stream, Match, ValueCharArray.GetData(), StreamLen + 1, bShouldStopOnSeparator) )
		{
			TempValue.Shrink();
			Value = MoveTemp(TempValue);
			return true;
		}
	}

	return false;
}

// 
// Parse a quoted string.
//
bool FParse::QuotedString( const TCHAR* Buffer, FString& Value, int32* OutNumCharsRead )
{
	if (OutNumCharsRead)
	{
		*OutNumCharsRead = 0;
	}

	const TCHAR* Start = Buffer;

	// Require opening quote
	if (*Buffer++ != TCHAR('"'))
	{
		return false;
	}

	auto ShouldParse = [](const TCHAR Ch)
	{
		return Ch != 0 && Ch != TCHAR('"') && Ch != TCHAR('\n') && Ch != TCHAR('\r');
	};

	while (ShouldParse(*Buffer))
	{
		if (*Buffer != TCHAR('\\')) // unescaped character
		{
			Value += *Buffer++;
		}
		else if (*++Buffer == TCHAR('\\')) // escaped backslash "\\"
		{
			Value += TEXT("\\");
			++Buffer;
		}
		else if (*Buffer == TCHAR('"')) // escaped double quote "\""
		{
			Value += TCHAR('"');
			++Buffer;
		}
		else if (*Buffer == TCHAR('\'')) // escaped single quote "\'"
		{
			Value += TCHAR('\'');
			++Buffer;
		}
		else if (*Buffer == TCHAR('n')) // escaped newline
		{
			Value += TCHAR('\n');
			++Buffer;
		}
		else if (*Buffer == TCHAR('r')) // escaped carriage return
		{
			Value += TCHAR('\r');
			++Buffer;
		}
		else if (*Buffer == TCHAR('t')) // escaped tab
		{
			Value += TCHAR('\t');
			++Buffer;
		}
		else if (FChar::IsOctDigit(*Buffer)) // octal sequence (\012)
		{
			FString OctSequence;
			while (ShouldParse(*Buffer) && FChar::IsOctDigit(*Buffer) && OctSequence.Len() < 3) // Octal sequences can only be up-to 3 digits long
			{
				OctSequence += *Buffer++;
			}

			Value += (TCHAR)FCString::Strtoi(*OctSequence, nullptr, 8);
		}
		else if (*Buffer == TCHAR('x')) // hex sequence (\xBEEF)
		{
			++Buffer;

			FString HexSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer))
			{
				HexSequence += *Buffer++;
			}

			Value += (TCHAR)FCString::Strtoi(*HexSequence, nullptr, 16);
		}
		else if (*Buffer == TCHAR('u')) // UTF-16 sequence (\u1234)
		{
			++Buffer;

			FString UnicodeSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer) && UnicodeSequence.Len() < 4) // UTF-16 sequences can only be up-to 4 digits long
			{
				UnicodeSequence += *Buffer++;
			}

			const uint32 UnicodeCodepoint = (uint32)FCString::Strtoi(*UnicodeSequence, nullptr, 16);

			FString UnicodeString;
			if (FUnicodeChar::CodepointToString(UnicodeCodepoint, UnicodeString))
			{
				Value += MoveTemp(UnicodeString);
			}
		}
		else if (*Buffer == TCHAR('U')) // UTF-32 sequence (\U12345678)
		{
			++Buffer;

			FString UnicodeSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer) && UnicodeSequence.Len() < 8) // UTF-32 sequences can only be up-to 8 digits long
			{
				UnicodeSequence += *Buffer++;
			}

			const uint32 UnicodeCodepoint = (uint32)FCString::Strtoi(*UnicodeSequence, nullptr, 16);

			FString UnicodeString;
			if (FUnicodeChar::CodepointToString(UnicodeCodepoint, UnicodeString))
			{
				Value += MoveTemp(UnicodeString);
			}
		}
		else // unhandled escape sequence
		{
			Value += TEXT("\\");
			Value += *Buffer++;
		}
	}

	// Require closing quote
	if (*Buffer++ != TCHAR('"'))
	{
		return false;
	}

	if (OutNumCharsRead)
	{
		*OutNumCharsRead = (Buffer - Start);
	}

	return true;
}

// 
// Parse an Text token
// This is expected to in the form NSLOCTEXT("Namespace","Key","SourceString") or LOCTEXT("Key","SourceString")
//
bool FParse::Text( const TCHAR* Buffer, FText& Value, const TCHAR* Namespace )
{
	return FTextStringHelper::ReadFromString(Buffer, Value, Namespace);
}

// 
// Parse an Text.
// This is expected to in the form NSLOCTEXT("Namespace","Key","SourceString") or LOCTEXT("Key","SourceString")
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, FText& Value, const TCHAR* Namespace )
{
	// The FText 
	Stream = FCString::Strifind( Stream, Match );
	if( Stream )
	{
		Stream += FCString::Strlen( Match );
		return FParse::Text( Stream, Value, Namespace );
	}

	return false;
}

//
// Parse a quadword.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, uint64& Value )
{
	return FParse::Value( Stream, Match, *(int64*)&Value );
}

//
// Parse a signed quadword.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, int64& Value )
{
	TCHAR Temp[4096]=TEXT(""), *Ptr=Temp;
	if( FParse::Value( Stream, Match, Temp, ARRAY_COUNT(Temp) ) )
	{
		Value = 0;
		bool Negative = (*Ptr=='-');
		Ptr += Negative;
		while( *Ptr>='0' && *Ptr<='9' )
			Value = Value*10 + *Ptr++ - '0';
		if( Negative )
			Value = -Value;
		return true;
	}
	else
	{
		return false;
	}
}

//
// Get a name.
//
bool FParse::Value(	const TCHAR* Stream, const TCHAR* Match, FName& Name )
{
	TCHAR TempStr[NAME_SIZE];

	if( !FParse::Value(Stream,Match,TempStr,NAME_SIZE) )
	{
		return false;
	}

	Name = FName(TempStr);

	return true;
}

//
// Get a uint32.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, uint32& Value )
{
	const TCHAR* Temp = FCString::Strifind(Stream,Match);
	TCHAR* End;
	if( Temp==NULL )
		return false;
	Value = FCString::Strtoi( Temp + FCString::Strlen(Match), &End, 10 );

	return true;
}

//
// Get a byte.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, uint8& Value )
{
	const TCHAR* Temp = FCString::Strifind(Stream,Match);
	if( Temp==NULL )
		return false;
	Temp += FCString::Strlen( Match );
	Value = (uint8)FCString::Atoi( Temp );
	return Value!=0 || FChar::IsDigit(Temp[0]);
}

//
// Get a signed byte.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, int8& Value )
{
	const TCHAR* Temp = FCString::Strifind(Stream,Match);
	if( Temp==NULL )
		return false;
	Temp += FCString::Strlen( Match );
	Value = FCString::Atoi( Temp );
	return Value!=0 || FChar::IsDigit(Temp[0]);
}

//
// Get a word.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, uint16& Value )
{
	const TCHAR* Temp = FCString::Strifind( Stream, Match );
	if( Temp==NULL )
		return false;
	Temp += FCString::Strlen( Match );
	Value = (uint16)FCString::Atoi( Temp );
	return Value!=0 || FChar::IsDigit(Temp[0]);
}

//
// Get a signed word.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, int16& Value )
{
	const TCHAR* Temp = FCString::Strifind( Stream, Match );
	if( Temp==NULL )
		return false;
	Temp += FCString::Strlen( Match );
	Value = (int16)FCString::Atoi( Temp );
	return Value!=0 || FChar::IsDigit(Temp[0]);
}

//
// Get a floating-point number.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, float& Value )
{
	const TCHAR* Temp = FCString::Strifind( Stream, Match );
	if( Temp==NULL )
		return false;
	Value = FCString::Atof( Temp+FCString::Strlen(Match) );
	return true;
}

//
// Get a signed double word.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, int32& Value )
{
	const TCHAR* Temp = FCString::Strifind( Stream, Match );
	if( Temp==NULL )
		return false;
	Value = FCString::Atoi( Temp + FCString::Strlen(Match) );
	return true;
}

//
// Get a boolean value.
//
bool FParse::Bool( const TCHAR* Stream, const TCHAR* Match, bool& OnOff )
{
	TCHAR TempStr[16];
	if( FParse::Value( Stream, Match, TempStr, 16 ) )
	{
		OnOff = FCString::ToBool(TempStr);
		return true;
	}
	else
	{
		return false;
	}
}

//
// Get a globally unique identifier.
//
bool FParse::Value( const TCHAR* Stream, const TCHAR* Match, struct FGuid& Guid )
{
	TCHAR Temp[256];
	if( !FParse::Value( Stream, Match, Temp, ARRAY_COUNT(Temp) ) )
		return false;

	Guid.A = Guid.B = Guid.C = Guid.D = 0;
	if( FCString::Strlen(Temp)==32 )
	{
		TCHAR* End;
		Guid.D = FCString::Strtoi( Temp+24, &End, 16 ); Temp[24]=0;
		Guid.C = FCString::Strtoi( Temp+16, &End, 16 ); Temp[16]=0;
		Guid.B = FCString::Strtoi( Temp+8,  &End, 16 ); Temp[8 ]=0;
		Guid.A = FCString::Strtoi( Temp+0,  &End, 16 ); Temp[0 ]=0;
	}
	return true;
}


//
// Sees if Stream starts with the named command.  If it does,
// skips through the command and blanks past it.  Returns 1 of match,
// 0 if not.
//
bool FParse::Command( const TCHAR** Stream, const TCHAR* Match, bool bParseMightTriggerExecution )
{
#if !UE_BUILD_SHIPPING
	if(GConsoleCommandLibrary)
	{
		GConsoleCommandLibrary->OnParseCommand(Match);
		
		if(bParseMightTriggerExecution)
		{
			// Better we fail the test - we only wanted to find all commands.
			return false;
		}
	}
#endif // !UE_BUILD_SHIPPING

	while (**Stream == TEXT(' ') || **Stream == TEXT('\t'))
	{
		(*Stream)++;
	}

	int32 MatchLen = FCString::Strlen(Match);
	if (FCString::Strnicmp(*Stream, Match, MatchLen) == 0)
	{
		*Stream += MatchLen;
		if( !FChar::IsAlnum(**Stream))
//		if( !FChar::IsAlnum(**Stream) && (**Stream != '_') && (**Stream != '.'))		// more correct e.g. a cvar called "log.abc" should work but breaks some code so commented out
		{
			while (**Stream == TEXT(' ') || **Stream == TEXT('\t'))
			{
				(*Stream)++;
			}

			return true; // Success.
		}
		else
		{
			*Stream -= MatchLen;
			return false; // Only found partial match.
		}
	}
	else
	{
		return false; // No match.
	}
}

//
// Get next command.  Skips past comments and cr's.
//
void FParse::Next( const TCHAR** Stream )
{
	// Skip over spaces, tabs, cr's, and linefeeds.
	SkipJunk:
	while( **Stream==' ' || **Stream==9 || **Stream==13 || **Stream==10 )
		++*Stream;

	if( **Stream==';' )
	{
		// Skip past comments.
		while( **Stream!=0 && **Stream!=10 && **Stream!=13 )
			++*Stream;
		goto SkipJunk;
	}

	// Upon exit, *Stream either points to valid Stream or a nul.
}

//
// Grab the next space-delimited string from the input stream.
// If quoted, gets entire quoted string.
//
bool FParse::Token( const TCHAR*& Str, TCHAR* Result, int32 MaxLen, bool UseEscape )
{
	int32 Len=0;

	// Skip preceeding spaces and tabs.
	while( FChar::IsWhitespace(*Str) )
	{
		Str++;
	}

	if( *Str == TEXT('"') )
	{
		// Get quoted string.
		Str++;
		while( *Str && *Str!=TEXT('"') && (Len+1)<MaxLen )
		{
			TCHAR c = *Str++;
			if( c=='\\' && UseEscape )
			{
				// Get escape.
				c = *Str++;
				if( !c )
				{
					break;
				}
			}
			if( (Len+1)<MaxLen )
			{
				Result[Len++] = c;
			}
		}
		if( *Str==TEXT('"') )
		{
			Str++;
		}
	}
	else
	{
		// Get unquoted string (that might contain a quoted part, which will be left intact).
		// For example, -ARG="foo bar baz", will be treated as one token, with quotes intact
		bool bInQuote = false;

		while (1)
		{
			TCHAR Character = *Str;
			if ((Character == 0) || (FChar::IsWhitespace(Character) && !bInQuote))
			{
				break;
			}
			Str++;

			// Preserve escapes if they're in a quoted string (the check for " is in the else to let \" work as expected)
			if (Character == TEXT('\\') && UseEscape && bInQuote)
			{
				if ((Len+1) < MaxLen)
				{
					Result[Len++] = Character;
				}

				Character = *Str;
				if (!Character)
				{
					break;
				}
				Str++;
			}
			else if (Character == TEXT('"'))
			{
				bInQuote = !bInQuote;
			}

			if( (Len+1)<MaxLen )
			{
				Result[Len++] = Character;
			}
		}
	}
	Result[Len]=0;
	return Len!=0;
}

bool FParse::Token( const TCHAR*& Str, FString& Arg, bool UseEscape )
{
	Arg.Empty();

	// Skip preceeding spaces and tabs.
	while( FChar::IsWhitespace(*Str) )
	{
		Str++;
	}

	if ( *Str == TEXT('"') )
	{
		// Get quoted string.
		Str++;
		while( *Str && *Str != TCHAR('"') )
		{
			TCHAR c = *Str++;
			if( c==TEXT('\\') && UseEscape )
			{
				// Get escape.
				c = *Str++;
				if( !c )
				{
					break;
				}
			}

			Arg += c;
		}

		if ( *Str == TEXT('"') )
		{
			Str++;
		}
	}
	else
	{
		// Get unquoted string (that might contain a quoted part, which will be left intact).
		// For example, -ARG="foo bar baz", will be treated as one token, with quotes intact
		bool bInQuote = false;

		while (1)
		{
			TCHAR Character = *Str;
			if ((Character == 0) || (FChar::IsWhitespace(Character) && !bInQuote))
			{
				break;
			}
			Str++;

			// Preserve escapes if they're in a quoted string (the check for " is in the else to let \" work as expected)
			if (Character == TEXT('\\') && UseEscape && bInQuote)
			{
				Arg += Character;

				Character = *Str;
				if (!Character)
				{
					break;
				}
				Str++;
			}
			else if (Character == TEXT('"'))
			{
				bInQuote = !bInQuote;
			}

			Arg += Character;
		}
	}

	return Arg.Len() > 0;
}
FString FParse::Token( const TCHAR*& Str, bool UseEscape )
{
	TCHAR Buffer[1024];
	if( FParse::Token( Str, Buffer, ARRAY_COUNT(Buffer), UseEscape ) )
		return Buffer;
	else
		return TEXT("");
}

bool FParse::AlnumToken(const TCHAR*& Str, FString& Arg)
{
	Arg.Empty();

	// Skip preceeding spaces and tabs.
	while (FChar::IsWhitespace(*Str))
	{
		Str++;
	}

	while (FChar::IsAlnum(*Str) || *Str == TEXT('_'))
	{
		Arg += *Str;
		Str++;
	}

	return Arg.Len() > 0;
}

//
// Get a line of Stream (everything up to, but not including, CR/LF.
// Returns 0 if ok, nonzero if at end of stream and returned 0-length string.
//
bool FParse::Line(const TCHAR** Stream, TCHAR* Result, int32 MaxLen, bool bExact)
{
	bool bGotStream = false;
	bool bIsQuoted = false;
	bool bIgnore = false;

	*Result=0;
	while (**Stream != TEXT('\0') && **Stream != TEXT('\n') && **Stream != TEXT('\r') && --MaxLen > 0)
	{
		// Start of comments.
		if (!bIsQuoted && !bExact && (*Stream)[0]=='/' && (*Stream)[1] == TEXT('/'))
		{
			bIgnore = true;
		}
		
		// Command chaining.
		if (!bIsQuoted && !bExact && **Stream == TEXT('|'))
		{
			break;
		}

		// Check quoting.
		bIsQuoted = bIsQuoted ^ (**Stream == TEXT('\"'));
		bGotStream = true;

		// Got stuff.
		if (!bIgnore)
		{
			*(Result++) = *((*Stream)++);
		}
		else
		{
			(*Stream)++;
		}
	}

	if (bExact)
	{
		// Eat up exactly one CR/LF.
		if (**Stream == TEXT('\r'))
		{
			(*Stream)++;
		}

		if (**Stream == TEXT('\n'))
		{
			(*Stream)++;
		}
	}
	else
	{
		// Eat up all CR/LF's.
		while (**Stream == TEXT('\n') || **Stream == TEXT('\r') || **Stream == TEXT('|'))
		{
			(*Stream)++;
		}
	}

	*Result = TEXT('\0');
	return **Stream != TEXT('\0') || bGotStream;
}

bool FParse::Line(const TCHAR** Stream, FString& Result, bool bExact)
{
	bool bGotStream = false;
	bool bIsQuoted = false;
	bool bIgnore = false;

	Result = TEXT("");

	while (**Stream != TEXT('\0') && **Stream != TEXT('\n') && **Stream != TEXT('\r'))
	{
		// Start of comments.
		if (!bIsQuoted && !bExact && (*Stream)[0] == TEXT('/') && (*Stream)[1] == TEXT('/'))
		{
			bIgnore = true;
		}

		// Command chaining.
		if (!bIsQuoted && !bExact && **Stream == TEXT('|'))
		{
			break;
		}

		// Check quoting.
		bIsQuoted = bIsQuoted ^ (**Stream == TEXT('\"'));
		bGotStream = true;

		// Got stuff.
		if (!bIgnore)
		{
			Result.AppendChar(*((*Stream)++));
		}
		else
		{
			(*Stream)++;
		}
	}

	if (bExact)
	{
		// Eat up exactly one CR/LF.
		if (**Stream == TEXT('\r'))
		{
			(*Stream)++;
		}
		if (**Stream == TEXT('\n'))
		{
			(*Stream)++;
		}
	}
	else
	{
		// Eat up all CR/LF's.
		while (**Stream == TEXT('\n') || **Stream == TEXT('\r') || **Stream == TEXT('|'))
		{
			(*Stream)++;
		}
	}

	return **Stream != TEXT('\0') || bGotStream;
}

bool FParse::LineExtended(const TCHAR** Stream, FString& Result, int32& LinesConsumed, bool bExact)
{
	bool bGotStream = false;
	bool bIsQuoted = false;
	bool bIgnore = false;
	int32 BracketDepth = 0;

	Result = TEXT("");
	LinesConsumed = 0;

	while (**Stream != TEXT('\0') && ((**Stream != TEXT('\n') && **Stream != TEXT('\r')) || BracketDepth > 0))
	{
		// Start of comments.
		if (!bIsQuoted && !bExact && (*Stream)[0] == TEXT('/') && (*Stream)[1] == TEXT('/'))
		{
			bIgnore = true;
		}

		// Command chaining.
		if (!bIsQuoted && !bExact && **Stream == TEXT('|'))
		{
			break;
		}

		bGotStream = true;

		// bracketed line break
		if (**Stream == TEXT('\n') || **Stream == TEXT('\r'))
		{
			checkSlow(BracketDepth > 0);

			Result.AppendChar(TEXT(' '));
			LinesConsumed++;
			(*Stream)++;
			if (**Stream == TEXT('\n') || **Stream == TEXT('\r'))
			{
				(*Stream)++;
			}
		}
		// allow line break if the end of the line is a backslash
		else if (!bIsQuoted && (*Stream)[0] == TEXT('\\') && ((*Stream)[1] == TEXT('\n') || (*Stream)[1] == TEXT('\r')))
		{
			Result.AppendChar(TEXT(' '));
			LinesConsumed++;
			(*Stream) += 2;
			if (**Stream == TEXT('\n') || **Stream == TEXT('\r'))
			{
				(*Stream)++;
			}
		}
		// check for starting or ending brace
		else if (!bIsQuoted && **Stream == TEXT('{'))
		{
			BracketDepth++;
			(*Stream)++;
		}
		else if (!bIsQuoted && **Stream == TEXT('}') && BracketDepth > 0)
		{
			BracketDepth--;
			(*Stream)++;
		}
		// specifically consume escaped backslashes and quotes within quoted strings
		else if (bIsQuoted && !bIgnore && (*Stream)[0] == TEXT('\\') && ( (*Stream)[1] == TEXT('\"') || (*Stream)[1] == TEXT('\\') ))
		{
			Result.AppendChars(*Stream, 2);
			(*Stream) += 2;
		}
		else
		{
			bIsQuoted = bIsQuoted ^ (**Stream == TEXT('\"'));

			// Got stuff.
			if (!bIgnore)
			{
				Result.AppendChar(*((*Stream)++));
			}
			else
			{
				(*Stream)++;
			}
		}
	}
	if (**Stream == 0)
	{
		if (bGotStream)
		{
			LinesConsumed++;
		}
	}
	else if (bExact)
	{
		// Eat up exactly one CR/LF.
		if (**Stream == TEXT('\r') || **Stream == TEXT('\n'))
		{
			LinesConsumed++;
			if (**Stream == TEXT('\r'))
			{
				(*Stream)++;
			}
			if (**Stream == TEXT('\n'))
			{
				(*Stream)++;
			}
		}
	}
	else
	{
		// Eat up all CR/LF's.
		while (**Stream == TEXT('\n') || **Stream == TEXT('\r') || **Stream == TEXT('|'))
		{
			if (**Stream != TEXT('|'))
			{
				LinesConsumed++;
			}
			if (((*Stream)[0] == TEXT('\n') && (*Stream)[1] == TEXT('\r')) || ((*Stream)[0] == TEXT('\r') && (*Stream)[1] == TEXT('\n')))
			{
				(*Stream)++;
			}
			(*Stream)++;
		}
	}

	return **Stream != TEXT('\0') || bGotStream;
}

uint32 FParse::HexNumber(const TCHAR* HexString)
{
	uint32 Ret = 0;

	while (*HexString)
	{
		Ret *= 16;
		Ret += FParse::HexDigit(*HexString++);
	}

	return Ret;
}

uint64 FParse::HexNumber64(const TCHAR* HexString)
{
	uint64 Ret = 0;

	while (*HexString)
	{
		Ret *= 16;
		Ret += FParse::HexDigit(*HexString++);
	}

	return Ret;
}

bool FParse::SchemeNameFromURI(const TCHAR* URI, FString& OutSchemeName)
{
	for(int32 Idx = 0;;Idx++)
	{
		if(!FChar::IsAlpha(URI[Idx]) && !FChar::IsDigit(URI[Idx]) && URI[Idx] != TEXT('+') && URI[Idx] != TEXT('.') && URI[Idx] != TEXT('-'))
		{
			if(URI[Idx] == TEXT(':') && Idx > 0)
			{
				OutSchemeName = FString(Idx, URI);
				return true;
			}
			return false;
		}
	}
}


#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParseLineExtendedTest, "System.Core.Misc.ParseLineExtended", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FParseLineExtendedTest::RunTest(const FString& Parameters)
{
	const TCHAR* Tests[] = {
		TEXT("Test string"),                            // Normal string
		TEXT("{Test string}"),                          // Braced string
		TEXT("\"Test string\""),                        // Quoted string
		TEXT("\"Test \\\"string\\\"\""),                // Quoted string w/ escaped quotes
		TEXT("a=\"Test\", b=\"Test\""),                 // Quoted value list
		TEXT("a=\"Test\\\\\", b=\"{Test}\""),           // Quoted value list w/ escaped backslash preceeding closing quote
		TEXT("a=\"Test\\\\\\\" String\", b=\"{Test}\""),// Quoted value list w/ escaped backslash preceeding escaped quote
		TEXT("Test=(Inner=\"{content}\")"),             // Nested value list
	};

	const TCHAR* Expected[] = {
		TEXT("Test string"),
		TEXT("Test string"),
		TEXT("\"Test string\""),
		TEXT("\"Test \\\"string\\\"\""),
		TEXT("a=\"Test\", b=\"Test\""),
		TEXT("a=\"Test\\\\\", b=\"{Test}\""),
		TEXT("a=\"Test\\\\\\\" String\", b=\"{Test}\""),
		TEXT("Test=(Inner=\"{content}\")"),
	};

	int32 LinesConsumed = 0;
	FString Result;

	for (int32 Index = 0; Index < ARRAY_COUNT(Tests); ++Index)
	{
		LinesConsumed = 0;
		Result.Reset();

		const TCHAR* Stream = Tests[Index];
		bool bSuccess = FParse::LineExtended(&Stream, Result, LinesConsumed, false);
		TestTrue(*FString::Printf(TEXT("Expecting parsed line [%s] to be [%s]. Result was [%s]."), Tests[Index], Expected[Index], *Result), bSuccess && Result == Expected[Index]);
	}

	return true;
}

#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
