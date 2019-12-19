// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimPreviewInstance.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "PhysicsAssetEditorAnimInstanceProxy.generated.h"

class UAnimSequence;

/** Proxy override for this UAnimInstance-derived class */
USTRUCT()
struct FPhysicsAssetEditorAnimInstanceProxy : public FAnimPreviewInstanceProxy
{
	GENERATED_BODY()

public:
	FPhysicsAssetEditorAnimInstanceProxy()
	{
	}

	FPhysicsAssetEditorAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimPreviewInstanceProxy(InAnimInstance)
	{
	}

	virtual ~FPhysicsAssetEditorAnimInstanceProxy();

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

private:
	void ConstructNodes();

	FAnimNode_RigidBody RagdollNode;
 	FAnimNode_ConvertComponentToLocalSpace ComponentToLocalSpace;
};