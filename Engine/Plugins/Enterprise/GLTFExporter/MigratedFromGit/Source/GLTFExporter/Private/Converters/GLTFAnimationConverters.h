// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class ULevelSequence;
class ALevelSequenceActor;

typedef TGLTFConverter<FGLTFJsonAnimation*, FGLTFJsonNode*, const USkeletalMesh*, const UAnimSequence*> IGLTFAnimationConverter;
typedef TGLTFConverter<FGLTFJsonAnimation*, FGLTFJsonNode*, const USkeletalMeshComponent*> IGLTFAnimationDataConverter;
typedef TGLTFConverter<FGLTFJsonAnimation*, const ULevel*, const ULevelSequence*> IGLTFLevelSequenceConverter;
typedef TGLTFConverter<FGLTFJsonAnimation*, const ALevelSequenceActor*> IGLTFLevelSequenceDataConverter;

class FGLTFAnimationConverter final : public FGLTFBuilderContext, public IGLTFAnimationConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimation* Convert(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence) override;
};

class FGLTFAnimationDataConverter final : public FGLTFBuilderContext, public IGLTFAnimationDataConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimation* Convert(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent) override;
};

class FGLTFLevelSequenceConverter final : public FGLTFBuilderContext, public IGLTFLevelSequenceConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimation* Convert(const ULevel* Level, const ULevelSequence*) override;
};

class FGLTFLevelSequenceDataConverter final : public FGLTFBuilderContext, public IGLTFLevelSequenceDataConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonAnimation* Convert(const ALevelSequenceActor* LevelSequenceActor) override;
};
