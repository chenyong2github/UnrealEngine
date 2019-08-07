// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IAudioAnalyzerNRTInterface.h"
#include "CoreMinimal.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

namespace Audio
{
	void IAnalyzerNRTResult::CopyFrom(IAnalyzerNRTResult* SourceResult)
	{
		FBitWriter SerializedResult;
		SourceResult->Serialize(SerializedResult);
		FBitReader DeserializedResult = FBitReader(SerializedResult.GetData(), SerializedResult.GetNumBytes() * 8);
		Serialize(DeserializedResult);
	}
	
	FString IAnalyzerNRTResult::ToString()
	{
		return FString(TEXT("To use, override ToString() in this implementation of IAnalyzerNRTResult."));
	}

	FName IAnalyzerNRTFactory::GetModularFeatureName()
	{
		static FName AudioExtFeatureName = FName(TEXT("AudioAnalyzerNRTPlugin"));
		return AudioExtFeatureName;
	}

	// This is used to identify analyzers created with this factory.
	FName IAnalyzerNRTFactory::GetName() const
	{
		static FName DefaultName(TEXT("UnnamedAudioAnalyzerNRT"));
		return DefaultName;
	}

	FString IAnalyzerNRTFactory::GetTitle() const
	{
		return FString("Unnamed Non Real-Time Audio Analyzer.");
	}
}
