// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_IKRig.h"
#include "Animation/AnimInstance.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_IKRig 

#define LOCTEXT_NAMESPACE "AnimGraphNode_IKRig"
const FName UAnimGraphNode_IKRig::AnimModeName(TEXT("IKRig.IKRigEditor.IKRigEditMode"));

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

	// copies Pin values from the internal node to get data which are not compiled yet
	//Fabrik->EffectorTransform = Node.EffectorTransform;
}

FEditorModeID UAnimGraphNode_IKRig::GetEditorMode() const
{
// we have a crash on shutting it down because it's not unregistering
// https://epic.slack.com/archives/C024FSXHJ/p1606256645266900
// until we know what is solution, I'm disabling it
//	return AnimModeName;

	return NAME_None;
}

void UAnimGraphNode_IKRig::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	// Reconstruct node to show updates to PinFriendlyNames.
	if ((PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRig, RigDefinitionAsset)))
	{
		Node.RebuildGoalList();
		ReconstructNode();
	}
}

void UAnimGraphNode_IKRig::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	FString PinString = Pin->PinName.ToString();
	FString GoalPinString = GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRig, GoalTransforms);
	const int32 GoalStringLen = GoalPinString.Len();
	if (PinString.Len() > GoalStringLen && PinString.Left(GoalStringLen) == GoalPinString)
	{
		if (!Pin->bHidden)
		{
			Pin->PinFriendlyName = FText::FromName(Node.GetGoalName(ArrayIndex));
		}
	}
}
#undef LOCTEXT_NAMESPACE
