// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonAnimationPlayback.h"

struct GLTFEXPORTER_API FGLTFJsonAnimationChannelTarget : IGLTFJsonObject
{
	FGLTFJsonNodeIndex Node;
	EGLTFJsonTargetPath Path;

	FGLTFJsonAnimationChannelTarget()
		: Path(EGLTFJsonTargetPath::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimationChannel : IGLTFJsonObject
{
	FGLTFJsonAnimationSamplerIndex Sampler;
	FGLTFJsonAnimationChannelTarget Target;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimationSampler : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Input;
	FGLTFJsonAccessorIndex Output;

	EGLTFJsonInterpolation Interpolation;

	FGLTFJsonAnimationSampler()
		: Interpolation(EGLTFJsonInterpolation::Linear)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimation : IGLTFJsonIndexedObject
{
	FString Name;

	TArray<FGLTFJsonAnimationChannel> Channels;
	TArray<FGLTFJsonAnimationSampler> Samplers;

	FGLTFJsonAnimationPlayback Playback;

	FGLTFJsonAnimation(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
