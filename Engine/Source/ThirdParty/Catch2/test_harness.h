// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef AUDIO_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#define AUDIO_ENABLE_TESTHARNESS_ENGINE_SUPPORT 1
#endif

#if AUDIO_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#endif

#ifdef _MSC_VER
#pragma warning(disable: 4005 4582 4583) // 4005 is a macro redefinition. It's for "TEXT" and harmless in this case. 4582 and 4583 shouldn't be enabled by the engine in the first place.
#endif
#include "catch.hpp"


#if AUDIO_ENABLE_TESTHARNESS_ENGINE_SUPPORT
inline std::string FStringToStdString(const FString& Value)
{
	static std::string quote(R"(")");
	return quote + std::string(TCHAR_TO_UTF8(*Value)) + quote;
}

namespace Catch
{
	// Tell Catch how to print FStrings
	template <>
	class StringMaker<FString>
	{
	public:
		static std::string convert(const FString& Value) { return FStringToStdString(Value); }
	};

	// Tell Catch how to print TPairs
	template <typename PairKeyType, typename PairValueType>
	class StringMaker<TPair<PairKeyType, PairValueType>>
	{
	public:
		static std::string convert(const TPair<PairKeyType, PairValueType>& Value)
		{
			return "{ " + StringMaker<PairKeyType>::convert(Value.Key) + " , " + StringMaker<PairValueType>::convert(Value.Value) + " }";
		}
	};
}

template <typename KeyT, typename ValueT>
static bool operator==(const TMap<KeyT, ValueT>& Left, const TMap<KeyT, ValueT>& Right)
{
	bool bIsEqual = Left.Num() == Right.Num();
	if (bIsEqual)
	{
		for (const auto& Pair : Left)
		{
			const ValueT* RightValue = Right.Find(Pair.Key);
			bIsEqual = bIsEqual && RightValue != nullptr && Pair.Value == *RightValue;
		}
	}
	return bIsEqual;
}

/**
 * Loop through a list of optional JSON keys and remove them from a test object.
 * Check that FromJson returns true for each individually removed.
 */
#define CHECK_OPTIONAL_JSON_KEYS(FullJsonSpec, OptionalKeys, Data) do {                          \
	SECTION("Checking optional keys") {                                                          \
		for (const FString& Key : OptionalKeys)                                                  \
		{                                                                                        \
			TSharedPtr<FJsonObject> TestJsonObject = MakeShared<FJsonObject>();                  \
			TSharedRef< TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(FullJsonSpec); \
			REQUIRE(FJsonSerializer::Deserialize(JsonReader, TestJsonObject));                   \
			TestJsonObject->RemoveField(Key);                                                    \
			FString OutputString;                                                                \
			TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);    \
			FJsonSerializer::Serialize(TestJsonObject.ToSharedRef(), Writer);                    \
			INFO("Checking optionality of key: " << FStringToStdString(Key));                    \
			CHECK(FromJson(OutputString, Data));                                                 \
		}                                                                                        \
	}                                                                                            \
} while (0)                                                                                      \
/**/

 /**
  * Loop through all the fields in an object and remove each of the required keys individually.
  * Check that FromJson returns false for each of the keys removed.
  */
#define CHECK_REQUIRED_JSON_KEYS(FullJsonSpec, OptionalKeys, Data) do {                          \
	SECTION("Checking required keys") {                                                          \
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();                          \
		TSharedRef< TJsonReader<> > JsonReaderOuter = TJsonReaderFactory<>::Create(FullJsonSpec);\
		REQUIRE(FJsonSerializer::Deserialize(JsonReaderOuter, JsonObject));                      \
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObject->Values)            \
		{                                                                                        \
			const FString& Key = Pair.Key;                                                       \
			if (OptionalKeys.Contains(Key))                                                      \
			{                                                                                    \
				continue;                                                                        \
			}                                                                                    \
			TSharedPtr<FJsonObject> TestJsonObject = MakeShared<FJsonObject>();                  \
			TSharedRef< TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(FullJsonSpec); \
			FJsonSerializer::Deserialize(JsonReader, TestJsonObject);                            \
			TestJsonObject->RemoveField(Key);                                                    \
			FString OutputString;                                                                \
			TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);    \
			FJsonSerializer::Serialize(TestJsonObject.ToSharedRef(), Writer);                    \
			INFO("Checking requiredness of key: " << FStringToStdString(Key));                   \
			CHECK_FALSE(FromJson(OutputString, Data));                                           \
		}                                                                                        \
	}                                                                                            \
} while (0)                                                                                      \
/**/

  /**
   * Loop through all the fields in a object and set to null individually.
   * Check that FromJson returns false for each of the keys set to null.
   */
#define CHECK_INVALID_JSON_VALUES(FullJsonSpec, Data) do {                                       \
	SECTION("Checking deserialization of invalid values") {                                      \
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();                          \
		TSharedRef< TJsonReader<> > JsonReaderOuter = TJsonReaderFactory<>::Create(FullJsonSpec);\
		REQUIRE(FJsonSerializer::Deserialize(JsonReaderOuter, JsonObject));                      \
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObject->Values)            \
		{                                                                                        \
			const FString& Key = Pair.Key;                                                       \
			TSharedPtr<FJsonObject> TestJsonObject = MakeShared<FJsonObject>();                  \
			TSharedRef< TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(FullJsonSpec); \
			FJsonSerializer::Deserialize(JsonReader, TestJsonObject);                            \
			TestJsonObject->SetField(Key, MakeShared<FJsonValueNull>());                         \
			FString OutputString;                                                                \
			TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);    \
			FJsonSerializer::Serialize(TestJsonObject.ToSharedRef(), Writer);                    \
			INFO("Checking validity with key: " << FStringToStdString(Key));                     \
			CHECK_FALSE(FromJson(OutputString, Data));                                           \
		}                                                                                        \
	}                                                                                            \
} while (0)                                                                                      \
/**/

#endif // #if AUDIO_ENABLE_TESTHARNESS_ENGINE_SUPPORT





