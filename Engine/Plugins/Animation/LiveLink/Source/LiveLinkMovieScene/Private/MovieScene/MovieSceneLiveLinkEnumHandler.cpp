// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieScene/MovieSceneLiveLinkEnumHandler.h"

#include "Channels/MovieSceneByteChannel.h"

#include "LiveLinkMovieScenePrivate.h"


//------------------------------------------------------------------------------
// FMovieSceneLiveLinkEnumHandler implementation.
//------------------------------------------------------------------------------

void FMovieSceneLiveLinkEnumHandler::CreateChannels(const UScriptStruct& InStruct, int32 InElementCount)
{
	UProperty* FoundProperty = PropertyBinding.GetProperty(InStruct);
	check(InElementCount > 0);

	if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(FoundProperty))
	{
		checkf(false, TEXT("Array of Enums are not supported."));
		check(ArrayProperty->Inner->IsA<UEnumProperty>());
	}
	else
	{
		check(FoundProperty->IsA<UEnumProperty>());
		check(InElementCount == 1);
	}

	PropertyStorage->ByteChannel.SetNum(InElementCount);
	ElementCount = InElementCount;
	Keys.SetNum(ElementCount);
}

void FMovieSceneLiveLinkEnumHandler::RecordFrame(const FFrameNumber& InFrameNumber, const UScriptStruct& InStruct, const FLiveLinkBaseFrameData* InFrameData) 
{
	if (InFrameData != nullptr)
	{
		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(PropertyBinding.GetProperty(InStruct)))
		{
			checkf(false, TEXT("Array of Enums are not supported."));
		}
		else
		{
			const int64 NewValue = PropertyBinding.GetCurrentValueForEnum(InStruct, InFrameData);
			FLiveLinkPropertyKey<int64> Key;
			Key.Time = InFrameNumber;
			Key.Value = NewValue;
			Keys[0].Add(Key);
		}
	}
}

void FMovieSceneLiveLinkEnumHandler::Finalize(bool bInReduceKeys, const FKeyDataOptimizationParams& InOptimizationParams)
{
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const TArray<FLiveLinkPropertyKey<int64>>& ElementKeys = Keys[i];
		for (const FLiveLinkPropertyKey<int64>& Key : ElementKeys)
		{
			PropertyStorage->ByteChannel[i].GetData().AddKey(Key.Time, Key.Value);
		}
	}

	if (bInReduceKeys)
	{
		// Reduce keys intentionally left blank
	}
}

void FMovieSceneLiveLinkEnumHandler::InitializeFromExistingChannels(const UScriptStruct& InStruct)
{
	ElementCount = PropertyStorage->ByteChannel.Num();
	check(ElementCount > 0);


	UProperty* FoundProperty = PropertyBinding.GetProperty(InStruct);
	if (FoundProperty)
	{
		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(FoundProperty))
		{
			checkf(false, TEXT("Array of Enums are not supported."));
			check(ArrayProperty->Inner->IsA<UEnumProperty>());
		}
		else
		{
			if (ElementCount > 1)
			{
				UE_LOG(LogLiveLinkMovieScene, Warning, TEXT("Initializing channels for property '%s' with %d elements. C-Style array aren't supported. Only one element will be used."), *FoundProperty->GetFName().ToString(), ElementCount);
			}

			check(FoundProperty->IsA<UEnumProperty>());
		}
	}
}

void FMovieSceneLiveLinkEnumHandler::FillFrame(int32 InKeyIndex, const FLiveLinkWorldTime& InWorldTime, const TOptional<FQualifiedFrameTime>& InTimecodeTime, const UScriptStruct& InStruct, FLiveLinkBaseFrameData* OutFrame)
{
	UProperty* FoundProperty = PropertyBinding.GetProperty(InStruct);
	if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(FoundProperty))
	{
		checkf(false, TEXT("Array of Enums are not supported."));
	}
	else
	{
		//C-Style arrays are not supported, only one value is used. 
		const int64 Value = GetChannelValue(InKeyIndex, 0);
		PropertyBinding.SetCurrentValueForEnum(InStruct, OutFrame, Value);
	}
}

void FMovieSceneLiveLinkEnumHandler::FillFrameInterpolated(const FFrameTime& InFrameTime, const FLiveLinkWorldTime& InWorldTime, const TOptional<FQualifiedFrameTime>& InTimecodeTime, const UScriptStruct& InStruct, FLiveLinkBaseFrameData* OutFrame)
{
	UProperty* FoundProperty = PropertyBinding.GetProperty(InStruct);
	if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(FoundProperty))
	{
		checkf(false, TEXT("Array of Enums are not supported."));
	}
	else
	{
		//C-Style arrays are not supported, only one value is used. 
		const int64 Value = GetChannelValueInterpolated(InFrameTime, 0);
		PropertyBinding.SetCurrentValueForEnum(InStruct, OutFrame, Value);
	}
}

int64 FMovieSceneLiveLinkEnumHandler::GetChannelValue(int32 InKeyIndex, int32 InChannelIndex)
{
	return PropertyStorage->ByteChannel[InChannelIndex].GetData().GetValues()[InKeyIndex];
}

int64 FMovieSceneLiveLinkEnumHandler::GetChannelValueInterpolated(const FFrameTime& InFrameTime, int32 InChannelIndex)
{
	uint8 Value;
	PropertyStorage->ByteChannel[InChannelIndex].Evaluate(InFrameTime, Value);
	return (int64)Value;
}

