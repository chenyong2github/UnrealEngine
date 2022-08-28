// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

template <typename... InputTypes>
class TGLTFAnimationConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAnimationIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFAnimationConverter final : public TGLTFAnimationConverter<FGLTFJsonNodeIndex, const USkeletalMesh*, const UAnimSequence*>
{
	using TGLTFAnimationConverter::TGLTFAnimationConverter;

	virtual FGLTFJsonAnimationIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence) override;
};

class FGLTFAnimationDataConverter final : public TGLTFAnimationConverter<FGLTFJsonNodeIndex, const USkeletalMeshComponent*>
{
	using TGLTFAnimationConverter::TGLTFAnimationConverter;

	virtual FGLTFJsonAnimationIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMeshComponent* SkeletalMeshComponent) override;
};
