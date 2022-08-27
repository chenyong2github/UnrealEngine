// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonAnimationPlayback.h"

struct FGLTFJsonAnimationChannelTarget : IGLTFJsonObject
{
	FGLTFJsonNodeIndex Node;
	EGLTFJsonTargetPath Path;

	FGLTFJsonAnimationChannelTarget()
		: Path(EGLTFJsonTargetPath::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("node"), Node);
		Writer.Write(TEXT("path"), Path);
	}
};

struct FGLTFJsonAnimationChannel : IGLTFJsonObject
{
	FGLTFJsonAnimationSamplerIndex Sampler;
	FGLTFJsonAnimationChannelTarget Target;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("sampler"), Sampler);

		Writer.Write(TEXT("target"), Target);
	}
};

struct FGLTFJsonAnimationSampler : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Input;
	FGLTFJsonAccessorIndex Output;

	EGLTFJsonInterpolation Interpolation;

	FGLTFJsonAnimationSampler()
		: Interpolation(EGLTFJsonInterpolation::Linear)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("input"), Input);
		Writer.Write(TEXT("output"), Output);

		if (Interpolation != EGLTFJsonInterpolation::Linear)
		{
			Writer.Write(TEXT("interpolation"), Interpolation);
		}
	}
};

struct FGLTFJsonAnimation : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonAnimationChannel> Channels;
	TArray<FGLTFJsonAnimationSampler> Samplers;

	FGLTFJsonAnimationPlayback Playback;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("channels"), Channels);
		Writer.Write(TEXT("samplers"), Samplers);

		if (Playback != FGLTFJsonAnimationPlayback())
		{
			Writer.StartExtensions();
			Writer.Write(EGLTFJsonExtension::EPIC_AnimationPlayback, Playback);
			Writer.EndExtensions();
		}
	}
};
