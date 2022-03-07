// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestHarness.h"

#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Misc/StringBuilder.h"

namespace UE::Private::LowLevelTestsRunner
{

std::string CStringToStdString(const TCHAR* Value)
{
	return StringViewToStdString(Value);
}

std::string FStringToStdString(const FString& Value)
{
	return StringViewToStdString(Value);
}

std::string StringViewToStdString(const FStringView& Value)
{
	const int32 ConvertedLen = FTCHARToUTF8_Convert::ConvertedLength(Value.GetData(), Value.Len());
	std::string String;
	String.reserve(ConvertedLen + 2);
	String.push_back('"');
	String.resize(ConvertedLen + 1);
	FTCHARToUTF8_Convert::Convert(String.data() + 1, ConvertedLen, Value.GetData(), Value.Len());
	String.push_back('"');
	return String;
}

} // UE::Private::LowLevelTestsRunner
