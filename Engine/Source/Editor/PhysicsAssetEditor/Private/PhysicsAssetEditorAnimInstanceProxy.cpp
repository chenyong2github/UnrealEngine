// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PhysicsAssetEditorAnimInstanceProxy.h"
#include "PhysicsAssetEditorAnimInstance.h"

void FPhysicsAssetEditorAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimPreviewInstanceProxy::Initialize(InAnimInstance);
	ConstructNodes();
}

void FPhysicsAssetEditorAnimInstanceProxy::ConstructNodes()
{
	ComponentToLocalSpace.ComponentPose.SetLinkNode(&RagdollNode);
	
	RagdollNode.SimulationSpace = ESimulationSpace::WorldSpace;
	RagdollNode.ActualAlpha = 1.0f;
}

FAnimNode_Base* FPhysicsAssetEditorAnimInstanceProxy::GetCustomRootNode()
{
	return &ComponentToLocalSpace;
}

void FPhysicsAssetEditorAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&RagdollNode);
	OutNodes.Add(&ComponentToLocalSpace);
}

void FPhysicsAssetEditorAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (CurrentAsset != nullptr)
	{
		FAnimPreviewInstanceProxy::UpdateAnimationNode(InContext);
	}
	else
	{
		ComponentToLocalSpace.Update_AnyThread(InContext);
	}
}

bool FPhysicsAssetEditorAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	if (CurrentAsset != nullptr)
	{
		return FAnimPreviewInstanceProxy::Evaluate_WithRoot(Output, InRootNode);
	}
	else
	{
		InRootNode->Evaluate_AnyThread(Output);
		return true;
	}
}

void FPhysicsAssetEditorAnimInstanceProxy::AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName)
{
	RagdollNode.AddImpulseAtLocation(Impulse, Location, BoneName);
}

FPhysicsAssetEditorAnimInstanceProxy::~FPhysicsAssetEditorAnimInstanceProxy()
{
}