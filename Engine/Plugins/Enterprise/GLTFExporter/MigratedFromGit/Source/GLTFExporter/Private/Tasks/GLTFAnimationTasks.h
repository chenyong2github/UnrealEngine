// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "LevelSequenceActor.h"
#include "LevelSequence.h"

class FGLTFAnimSequenceTask : public FGLTFTask
{
public:

	FGLTFAnimSequenceTask(FGLTFConvertBuilder& Builder,  FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence, FGLTFJsonAnimation* JsonAnimation)
		: FGLTFTask(EGLTFTaskPriority::Animation)
		, Builder(Builder)
		, RootNode(RootNode)
		, SkeletalMesh(SkeletalMesh)
		, AnimSequence(AnimSequence)
		, JsonAnimation(JsonAnimation)
	{
	}

	virtual FString GetName() override
	{
		return AnimSequence->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFJsonNode* RootNode;
	const USkeletalMesh* SkeletalMesh;
	const UAnimSequence* AnimSequence;
	FGLTFJsonAnimation* JsonAnimation;
};

class FGLTFLevelSequenceTask : public FGLTFTask
{
public:

	FGLTFLevelSequenceTask(FGLTFConvertBuilder& Builder, const ULevel* Level, const ULevelSequence* LevelSequence, FGLTFJsonAnimation* JsonAnimation)
		: FGLTFTask(EGLTFTaskPriority::Animation)
		, Builder(Builder)
		, Level(Level)
		, LevelSequence(LevelSequence)
		, JsonAnimation(JsonAnimation)
	{
	}

	virtual FString GetName() override
	{
		return LevelSequence->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const ULevel* Level;
	const ULevelSequence* LevelSequence;
	FGLTFJsonAnimation* JsonAnimation;
};
