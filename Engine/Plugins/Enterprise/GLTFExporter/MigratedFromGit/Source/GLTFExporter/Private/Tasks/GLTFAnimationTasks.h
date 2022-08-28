// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Engine.h"

class FGLTFAnimationTask : public FGLTFTask
{
public:

	FGLTFAnimationTask(FGLTFConvertBuilder& Builder, FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence, FGLTFJsonAnimationIndex AnimationIndex)
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
