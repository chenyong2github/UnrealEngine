// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_IKRetargeter.h"
#include "Animation/AnimInstance.h"
#include "Kismet2/CompilerResultsLog.h"

//#pragma optimize("", off)

#define LOCTEXT_NAMESPACE "AnimGraphNode_IKRig"
const FName UAnimGraphNode_IKRetargeter::AnimModeName(TEXT("IKRig.IKRigEditor.IKRigEditMode"));

void UAnimGraphNode_IKRetargeter::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const
{
}

FText UAnimGraphNode_IKRetargeter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNode_IKRetargeter_Title", "IK Retargeter");
}

void UAnimGraphNode_IKRetargeter::CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode)
{
	FAnimNode_IKRetargeter* IKRetargeterNode = static_cast<FAnimNode_IKRetargeter*>(InPreviewNode);
}

FEditorModeID UAnimGraphNode_IKRetargeter::GetEditorMode() const
{
	return AnimModeName;
}

void UAnimGraphNode_IKRetargeter::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	// hide the Source Mesh Component input pin when bAutoFindSourceMeshByTag is true
	const FString SourceMeshPropertyName = GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRetargeter, SourceMeshComponent);
	const FString SourcePropertyString = SourcePropertyName.ToString();
	if (SourcePropertyString == SourceMeshPropertyName)
	{
		Pin->bHidden = Node.bUseAttachedParent;	
	}
}

void UAnimGraphNode_IKRetargeter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);
	if ((PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRetargeter, bUseAttachedParent)))
	{
		ReconstructNode();
	}
}

void UAnimGraphNode_IKRetargeter::PostLoad()
{
	Super::PostLoad();
}

void UAnimGraphNode_IKRetargeter::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton,	FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	// validate source mesh component is not null
	if (!Node.bUseAttachedParent)
	{
		if (!IsPinExposedAndLinked(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_IKRetargeter, SourceMeshComponent)))
		{
			MessageLog.Warning(TEXT("@@ is missing a Source Skeletal Mesh Component reference."), this);
			return;
		}
	}

	// validate IK Rig asset has been assigned
	if (!Node.IKRetargeterAsset)
	{
		MessageLog.Warning(TEXT("@@ is missing an IKRetargeter asset."), this);
		return;
	}

	// validate SOURCE IK Rig asset has been assigned
	if (!Node.IKRetargeterAsset->SourceIKRigAsset)
	{
		MessageLog.Warning(TEXT("@@ has IK Retargeter that is missing a source IK Rig asset."), this);
	}

	// validate TARGET IK Rig asset has been assigned
	if (!Node.IKRetargeterAsset->TargetIKRigAsset)
	{
		MessageLog.Warning(TEXT("@@ has IK Retargeter that is missing a target IK Rig asset."), this);
	}

	if (!(Node.IKRetargeterAsset->SourceIKRigAsset && Node.IKRetargeterAsset->TargetIKRigAsset))
	{
		return;
	}
	
	// validate that target bone chains exist on this skeleton
	const FReferenceSkeleton &RefSkel = ForSkeleton->GetReferenceSkeleton();
	const TArray<FBoneChain> &TargetBoneChains = Node.IKRetargeterAsset->TargetIKRigAsset->RetargetDefinition.BoneChains;
    for (const FBoneChain &Chain : TargetBoneChains)
    {
        if (RefSkel.FindBoneIndex(Chain.StartBone) == INDEX_NONE)
        {
        	MessageLog.Warning(*LOCTEXT("StartBoneNotFound", "@@ - Start Bone in target IK Rig Bone Chain not found.").ToString(), this);
        }

    	if (RefSkel.FindBoneIndex(Chain.EndBone) == INDEX_NONE)
    	{
    		MessageLog.Warning(*LOCTEXT("EndBoneNotFound", "@@ - End Bone in target IK Rig Bone Chain not found.").ToString(), this);
    	}
    }
}

void UAnimGraphNode_IKRetargeter::PreloadRequiredAssets()
{
	Super::PreloadRequiredAssets();
	
	if (Node.IKRetargeterAsset)
	{
		PreloadObject(Node.IKRetargeterAsset);
		PreloadObject(Node.IKRetargeterAsset->SourceIKRigAsset);
		PreloadObject(Node.IKRetargeterAsset->TargetIKRigAsset);
	}
}

#undef LOCTEXT_NAMESPACE
