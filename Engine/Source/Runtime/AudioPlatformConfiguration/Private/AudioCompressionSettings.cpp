// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioCompressionSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AudioCompressionSettings)

FPlatformRuntimeAudioCompressionOverrides::FPlatformRuntimeAudioCompressionOverrides()
	: bOverrideCompressionTimes(false)
	, DurationThreshold(5.0f)
	, MaxNumRandomBranches(0)
	, SoundCueQualityIndex(0)
{
	
}

FPlatformRuntimeAudioCompressionOverrides* FPlatformRuntimeAudioCompressionOverrides::DefaultCompressionOverrides = nullptr;

// Increment this return value to force a recook on all Stream Caching assets.
// For testing, it's useful to set this to either a negative number or
// absurdly large number, to ensure you do not pollute the DDC.
int32 FPlatformAudioCookOverrides::GetStreamCachingVersion()
{
	return 5028;
}
namespace PlatformAudioCookOverridesPrivate
{
	template<typename HashType>
	void AppendHash(FString& OutString, const TCHAR* InName, const HashType& InValueToHash)
	{
		FString Lexed = LexToString(InValueToHash); // Using temporary prevents TCHAR/String mismatch in printf
		OutString += FString::Printf(TEXT("%s_%s_"), InName, *Lexed);
	}
}

void FPlatformAudioCookOverrides::GetHashSuffix(const FPlatformAudioCookOverrides* InOverrides, FString& OutSuffix)
{
	if (InOverrides == nullptr)
	{
		return;
	}	
	
	using namespace PlatformAudioCookOverridesPrivate;

	// Starting Delim is important, as FSoundWaveData::FindRuntimeFormat, uses it determine format from the inline chunk name.
	OutSuffix += TEXT("_");

	// Start with StreamCache version.
	AppendHash(OutSuffix, TEXT("SCVER"), GetStreamCachingVersion());
	
	// Each member in declaration order.
	
	// FPlatformAudioCookOverrides
	AppendHash(OutSuffix, TEXT("R4DV"), InOverrides->bResampleForDevice);

	TArray<float> Rates;
	InOverrides->PlatformSampleRates.GenerateValueArray(Rates);
	for (int32 i = 0; i < Rates.Num(); ++i)
	{
		AppendHash(OutSuffix, *FString::Printf(TEXT("SR%d"), i), Rates[i]);
	}

	AppendHash(OutSuffix, TEXT("QMOD"), InOverrides->CompressionQualityModifier);
	AppendHash(OutSuffix, TEXT("CQLT"), InOverrides->SoundCueCookQualityIndex);
	AppendHash(OutSuffix, TEXT("ASTH"), InOverrides->AutoStreamingThreshold);
	AppendHash(OutSuffix, TEXT("INLC"), InOverrides->bInlineStreamedAudioChunks);		

	// FAudioStreamCachingSettings
	AppendHash(OutSuffix, TEXT("CSZE"), InOverrides->StreamCachingSettings.CacheSizeKB);
	AppendHash(OutSuffix, TEXT("LCF"), InOverrides->StreamCachingSettings.bForceLegacyStreamChunking);
	AppendHash(OutSuffix, TEXT("ZCS"), InOverrides->StreamCachingSettings.ZerothChunkSizeForLegacyStreamChunkingKB);
	AppendHash(OutSuffix, TEXT("MCSO"), InOverrides->StreamCachingSettings.MaxChunkSizeOverrideKB);
	
	OutSuffix += TEXT("END");
}
