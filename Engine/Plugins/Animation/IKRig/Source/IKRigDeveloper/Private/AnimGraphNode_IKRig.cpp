// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_IKRig.h"
#include "Animation/AnimInstance.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_IKRig 

#define LOCTEXT_NAMESPACE "AnimGraphNode_IKRig"

void UAnimGraphNode_IKRig::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const
{
	if(PreviewSkelMeshComp)
	{
		if(FAnimNode_IKRig* ActiveNode = GetActiveInstanceNode<FAnimNode_IKRig>(PreviewSkelMeshComp->GetAnimInstance()))
		{
			ActiveNode->ConditionalDebugDraw(PDI, PreviewSkelMeshComp);
		}
	}
}

FText UAnimGraphNode_IKRig::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNode_IKRig_Title", "IK Rig");
}

void UAnimGraphNode_IKRig::CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode)
{
	FAnimNode_IKRig* IKRigNode = static_cast<FAnimNode_IKRig*>(InPreviewNode);
}

#undef LOCTEXT_NAMESPACE
