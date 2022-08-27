// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class ULevelSequence;
class ALevelSequenceActor;

typedef TGLTFConverter<FGLTFJsonAnimationIndex, FGLTFJsonNodeIndex, const USkeletalMesh*, const UAnimSequence*> IGLTFAnimationConverter;
typedef TGLTFConverter<FGLTFJsonAnimationIndex, FGLTFJsonNodeIndex, const USkeletalMeshComponent*> IGLTFAnimationDataConverter;
typedef TGLTFConverter<FGLTFJsonAnimationIndex, const ULevel*, const ULevelSequence*> IGLTFLevelSequenceConverter;
typedef TGLTFConverter<FGLTFJsonAnimationIndex, const ALevelSequenceActor*> IGLTFLevelSequenceDataConverter;

class FGLTFAnimationConverter final : public FGLTFBuilderContext, public IGLTFAnimationConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimationIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence) override;
};

class FGLTFAnimationDataConverter final : public FGLTFBuilderContext, public IGLTFAnimationDataConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimationIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMeshComponent* SkeletalMeshComponent) override;
};

class FGLTFLevelSequenceConverter final : public FGLTFBuilderContext, public IGLTFLevelSequenceConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimationIndex Convert(const ULevel* Level, const ULevelSequence*) override;
};

class FGLTFLevelSequenceDataConverter final : public FGLTFBuilderContext, public IGLTFLevelSequenceDataConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimationIndex Convert(const ALevelSequenceActor* LevelSequenceActor) override;
};
