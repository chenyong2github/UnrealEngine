// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
* Stores the raw rich curves as FCompressedRichCurve internally with optional key reduction and key time quantization.
*/

#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "AnimCurveCompressionCodec_UniformIndexable.generated.h"

struct ENGINE_API FAnimCurveBufferAccess
{
private:
	int32 NumSamples;
	float SampleRate;

	const float* CompressedBuffer;

	const FFloatCurve* RawCurve;
	bool bUseCompressedData;
public:

	UE_DEPRECATED(5.3, "Please use the constructor that takes a FName")
	FAnimCurveBufferAccess(const UAnimSequenceBase* InSequence, SmartName::UID_Type InUID);

	FAnimCurveBufferAccess(const UAnimSequenceBase* InSequence, FName InName);

	bool IsValid() const
	{
		return CompressedBuffer || RawCurve;
	}

	int32 GetNumSamples() const { return NumSamples; }

	float GetValue(int32 SampleIndex) const;

	float GetTime(int32 SampleIndex) const;
};

UCLASS(meta = (DisplayName = "Uniform Indexable"))
class ENGINE_API UAnimCurveCompressionCodec_UniformIndexable : public UAnimCurveCompressionCodec
{
	GENERATED_UCLASS_BODY()

	//////////////////////////////////////////////////////////////////////////

#if WITH_EDITORONLY_DATA
	// UAnimCurveCompressionCodec overrides
	virtual bool Compress(const FCompressibleAnimData& AnimSeq, FAnimCurveCompressionResult& OutResult) override;
	virtual void PopulateDDCKey(FArchive& Ar) override;
#endif
	
	virtual void DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const override;
	virtual float DecompressCurve(const FCompressedAnimSequence& AnimSeq, FName CurveName, float CurrentTime) const override;

	UE_DEPRECATED(5.3, "Please use GetCurveBufferAndSamples that takes a curve's FName")
	bool GetCurveBufferAndSamples(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, const float*& OutCurveBuffer, int32& OutSamples, float& OutSampleRate) { return false; }
	bool GetCurveBufferAndSamples(const FCompressedAnimSequence& AnimSeq, FName CurveName, const float*& OutCurveBuffer, int32& OutSamples, float& OutSampleRate);
};
