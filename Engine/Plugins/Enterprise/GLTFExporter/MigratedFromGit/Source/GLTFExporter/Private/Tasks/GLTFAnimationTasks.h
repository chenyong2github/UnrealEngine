// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "LevelSequenceActor.h"

class ALevelSequenceActor;

class FGLTFAnimSequenceTask : public FGLTFTask
{
public:

	FGLTFAnimSequenceTask(FGLTFConvertBuilder& Builder, FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence, FGLTFJsonAnimationIndex AnimationIndex)
        : FGLTFTask(EGLTFTaskPriority::Animation)
		, Builder(Builder)
		, RootNode(RootNode)
		, SkeletalMesh(SkeletalMesh)
		, AnimSequence(AnimSequence)
		, AnimationIndex(AnimationIndex)
	{
	}

	virtual FString GetName() override
	{
		return AnimSequence->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFJsonNodeIndex RootNode;
	const USkeletalMesh* SkeletalMesh;
	const UAnimSequence* AnimSequence;
	const FGLTFJsonAnimationIndex AnimationIndex;
};

class FGLTFLevelSequenceTask : public FGLTFTask
{
public:

	FGLTFLevelSequenceTask(FGLTFConvertBuilder& Builder, const ALevelSequenceActor* LevelSequenceActor, FGLTFJsonAnimationIndex AnimationIndex)
		: FGLTFTask(EGLTFTaskPriority::Animation)
		, Builder(Builder)
		, LevelSequenceActor(LevelSequenceActor)
		, AnimationIndex(AnimationIndex)
	{
	}

	virtual FString GetName() override
	{
		return LevelSequenceActor->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const ALevelSequenceActor* LevelSequenceActor;
	const FGLTFJsonAnimationIndex AnimationIndex;
};
