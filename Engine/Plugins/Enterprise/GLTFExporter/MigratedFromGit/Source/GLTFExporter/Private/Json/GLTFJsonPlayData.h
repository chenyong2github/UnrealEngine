// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonUtility.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonPlayData
{
	FString Name;

	bool Looping;
	bool Playing;

	float PlayRate;
	float Position;

	FGLTFJsonPlayData()
        : Looping(true)
        , Playing(true)
        , PlayRate(1)
        , Position(0)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		if (Looping != true)
		{
			JsonWriter.WriteValue(TEXT("looping"), Looping);
		}

		if (Playing != true)
		{
			JsonWriter.WriteValue(TEXT("playing"), Playing);
		}

		if (PlayRate != 1)
		{
			FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("playRate"), PlayRate);
		}

		if (Position != 0)
		{
			FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("position"), Position);
		}

		JsonWriter.WriteObjectEnd();
	}

	bool operator==(const FGLTFJsonPlayData& Other) const
	{
		return Looping == Other.Looping
            && Playing == Other.Playing
            && PlayRate == Other.PlayRate
            && Position == Other.Position;
	}

	bool operator!=(const FGLTFJsonPlayData& Other) const
	{
		return !(*this == Other);
	}
};
