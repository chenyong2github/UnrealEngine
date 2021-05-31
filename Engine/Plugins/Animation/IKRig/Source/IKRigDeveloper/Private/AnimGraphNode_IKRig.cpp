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
	if (PreviewSkelMeshComp)
	{
		if (FAnimNode_IKRig* ActiveNode = GetActiveInstanceNode<FAnimNode_IKRig>(PreviewSkelMeshComp->GetAnimInstance()))
		{
			//ActiveNode->ConditionalDebugDraw(PDI, PreviewSkelMeshComp);
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

void UAnimGraphNode_IKRig::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	// Reconstruct node to show updates to PinFriendlyNames.
	if ((PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRig, RigDefinitionAsset)))
	{
		if (Node.RebuildGoalList())
		{
			ReconstructNode();
		}
	}
}

void UAnimGraphNode_IKRig::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	// set names of GoalTransform pins to names of goals
	const FString PinString = Pin->PinName.ToString();
	const FString GoalPinString = GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRig, Goals);
	const int32 GoalStringLen = GoalPinString.Len();
	if (PinString.Len() > GoalStringLen && PinString.Left(GoalStringLen) == GoalPinString)
	{
		if (!Pin->bHidden)
		{
			Pin->PinFriendlyName = FText::FromName(Node.GetGoalName(ArrayIndex));
		}
	}
}

void UAnimGraphNode_IKRig::PostLoad()
{
	Super::PostLoad();
	Node.RebuildGoalList();
}

void UAnimGraphNode_IKRig::PreloadRequiredAssets()
{
	if (Node.RigDefinitionAsset)
	{
		PreloadObject(Node.RigDefinitionAsset);
		for (UIKRigSolver* Solver : Node.RigDefinitionAsset->Solvers)
		{
			PreloadObject(Solver);
		}
	}

	Super::PreloadRequiredAssets();
}
#undef LOCTEXT_NAMESPACE
