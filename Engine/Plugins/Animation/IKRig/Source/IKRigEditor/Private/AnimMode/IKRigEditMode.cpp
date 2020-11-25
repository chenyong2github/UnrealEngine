// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigEditMode.h"
#include "AnimGraphNode_IKRig.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"

void FIKRigEditMode::EnterMode(class UAnimGraphNode_Base* InEditorNode, struct FAnimNode_Base* InRuntimeNode)
{
	RuntimeNode = static_cast<FAnimNode_IKRig*>(InRuntimeNode);
	GraphNode = CastChecked<UAnimGraphNode_IKRig>(InEditorNode);

	FAnimNodeEditMode::EnterMode(InEditorNode, InRuntimeNode);
}

void FIKRigEditMode::ExitMode()
{
	RuntimeNode = nullptr;
	GraphNode = nullptr;

	FAnimNodeEditMode::ExitMode();
}

FVector FIKRigEditMode::GetWidgetLocation() const
{
	USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();

// 	FBoneSocketTarget& Target = RuntimeNode->EffectorTarget;
// 	FVector Location = RuntimeNode->EffectorTransform.GetLocation();
// 	EBoneControlSpace Space = RuntimeNode->EffectorTransformSpace;
// 	FVector WidgetLoc = ConvertWidgetLocation(SkelComp, RuntimeNode->ForwardedPose, Target, Location, Space);
	return SkelComp->GetComponentToWorld().GetLocation();
}

UE::Widget::EWidgetMode FIKRigEditMode::GetWidgetMode() const
{
	// allow translation all the time for effectot target
	return UE::Widget::WM_Translate;
}

void FIKRigEditMode::DoTranslation(FVector& InTranslation)
{
// 	USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();
// 	FVector Offset = ConvertCSVectorToBoneSpace(SkelComp, InTranslation, RuntimeNode->ForwardedPose, RuntimeNode->EffectorTarget, RuntimeNode->EffectorTransformSpace);
// 
// 	RuntimeNode->EffectorTransform.AddToTranslation(Offset);
// 	GraphNode->Node.EffectorTransform.SetTranslation(RuntimeNode->EffectorTransform.GetTranslation());
}
