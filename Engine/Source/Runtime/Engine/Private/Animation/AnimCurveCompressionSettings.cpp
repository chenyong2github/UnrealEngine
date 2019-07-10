// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec_CompressedRichCurve.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"

UAnimCurveCompressionSettings::UAnimCurveCompressionSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Codec = CreateDefaultSubobject<UAnimCurveCompressionCodec_CompressedRichCurve>(TEXT("CurveCompressionCodec"));
	Codec->SetFlags(RF_Transactional);
}

UAnimCurveCompressionCodec* UAnimCurveCompressionSettings::GetCodec(const FString& Path)
{
	return Codec->GetCodec(Path);
}

#if WITH_EDITORONLY_DATA
bool UAnimCurveCompressionSettings::AreSettingsValid() const
{
	return Codec != nullptr && Codec->IsCodecValid();
}

bool UAnimCurveCompressionSettings::Compress(const FCompressibleAnimData& AnimSeq, FCompressedAnimSequence& OutCompressedData) const
{
	if (Codec == nullptr || !AreSettingsValid())
	{
		return false;
	}

	FAnimCurveCompressionResult CompressionResult;
	bool Success = Codec->Compress(AnimSeq, CompressionResult);
	if (Success)
	{
		OutCompressedData.CompressedCurveByteStream = CompressionResult.CompressedBytes;
		OutCompressedData.CurveCompressionCodec = CompressionResult.Codec;
	}

	return Success;
}

void UAnimCurveCompressionSettings::PopulateDDCKey(FArchive& Ar) const
{
	if (Codec)
	{
		Codec->PopulateDDCKey(Ar);
	}
	else
	{
		static FString NoCodecString(TEXT("<Missing Codec>"));
		Ar << NoCodecString;
	}
}

#endif
