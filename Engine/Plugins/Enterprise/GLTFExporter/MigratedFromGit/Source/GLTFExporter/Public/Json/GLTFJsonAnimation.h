// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonAnimationPlayback.h"

struct GLTFEXPORTER_API FGLTFJsonAnimationChannelTarget : IGLTFJsonObject
{
	FGLTFJsonNode* Node;
	EGLTFJsonTargetPath Path;

	FGLTFJsonAnimationChannelTarget()
		: Node(nullptr)
		, Path(EGLTFJsonTargetPath::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimationChannel : IGLTFJsonObject
{
	FGLTFJsonAnimationSampler* Sampler;
	FGLTFJsonAnimationChannelTarget Target;

	FGLTFJsonAnimationChannel()
		: Sampler(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimationSampler : IGLTFJsonIndexedObject
{
	FGLTFJsonAccessor* Input;
	FGLTFJsonAccessor* Output;

	EGLTFJsonInterpolation Interpolation;

	FGLTFJsonAnimationSampler(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, Input(nullptr)
		, Output(nullptr)
		, Interpolation(EGLTFJsonInterpolation::Linear)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonAnimation : IGLTFJsonIndexedObject
{
	FString Name;

	TArray<FGLTFJsonAnimationChannel> Channels;
	TGLTFJsonIndexedObjectArray<FGLTFJsonAnimationSampler> Samplers;

	FGLTFJsonAnimationPlayback Playback;

	FGLTFJsonAnimation(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
