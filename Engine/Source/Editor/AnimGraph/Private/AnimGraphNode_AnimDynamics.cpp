// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_AnimDynamics.h"
#include "AnimNodeEditModes.h"
#include "EngineGlobals.h"
#include "SceneManagement.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Widgets/Input/SButton.h"

// Details includes
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "AnimationCustomVersion.h"
#include "Animation/AnimInstance.h"

#define LOCTEXT_NAMESPACE "AnimDynamicsNode"

FText UAnimGraphNode_AnimDynamics::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Anim Dynamics");
}

void UAnimGraphNode_AnimDynamics::GetOnScreenDebugInfo(TArray<FText>& DebugInfo, FAnimNode_Base* RuntimeAnimNode, USkeletalMeshComponent* PreviewSkelMeshComp) const
{
	if(RuntimeAnimNode)
	{
		const FAnimNode_AnimDynamics* PreviewNode = static_cast<FAnimNode_AnimDynamics*>(RuntimeAnimNode);

		for(const FAnimPhysBodyDefinition& PhysicsBodyDef : PreviewNode->PhysicsBodyDefinitions)
		{
			const FName BoneName = PhysicsBodyDef.BoundBone.BoneName;
			const int32 SkelBoneIndex = PreviewSkelMeshComp->GetBoneIndex(BoneName);
			if(SkelBoneIndex != INDEX_NONE)
			{
				FTransform BoneTransform = PreviewSkelMeshComp->GetBoneTransform(SkelBoneIndex);
				DebugInfo.Add(FText::Format(LOCTEXT("DebugOnScreenName", "Anim Dynamics (Bone:{0})"), FText::FromName(BoneName)));
				DebugInfo.Add(FText::Format(LOCTEXT("DebugOnScreenTranslation", "    Translation: {0}"), FText::FromString(BoneTransform.GetTranslation().ToString())));
				DebugInfo.Add(FText::Format(LOCTEXT("DebugOnScreenRotation", "    Rotation: {0}"), FText::FromString(BoneTransform.Rotator().ToString())));
			}
		}
	}
}

FText UAnimGraphNode_AnimDynamics::GetControllerDescription() const
{
	return LOCTEXT("Description", "Anim Dynamics");
}

void UAnimGraphNode_AnimDynamics::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	TSharedRef<IPropertyHandle> PreviewFlagHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAnimGraphNode_AnimDynamics, bPreviewLive));

	IDetailCategoryBuilder& PreviewCategory = DetailBuilder.EditCategory(TEXT("Preview"));
	PreviewCategory.AddProperty(PreviewFlagHandle);

	FDetailWidgetRow& WidgetRow = PreviewCategory.AddCustomRow(LOCTEXT("ResetButtonRow", "Reset"));

	WidgetRow
		[
			SNew(SButton)
			.Text(LOCTEXT("ResetButtonText", "Reset Simulation"))
			.ToolTipText(LOCTEXT("ResetButtonToolTip", "Resets the simulation for this node"))
			.OnClicked(FOnClicked::CreateStatic(&UAnimGraphNode_AnimDynamics::ResetButtonClicked, &DetailBuilder))
		];

	// Add warning message about physics body array not being updated untill after the first BP compile.
	if (LastPreviewComponent == nullptr)
	{
		IDetailCategoryBuilder& SetupCategory = DetailBuilder.EditCategory(TEXT("Setup"));
		const FText WarningText(LOCTEXT("AnimDynamicsWarningText", "WARNING - Physics Bodies Will Not Be Valid Untill This Node Has Been Connected And Compiled"));
		FDetailWidgetRow& WarningMessageTextWidgetRow = SetupCategory.AddCustomRow(WarningText);

		WarningMessageTextWidgetRow
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(WarningText)
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FLinearColor::Red)
				]
			];
	}

	// Force order of details panel catagories - Must set order for all of them as any that are edited automatically move to the top.
	{
		uint32 SortOrder = 0;
		DetailBuilder.EditCategory("Preview").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Setup").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Settings").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("SphericalLimit").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("PlanarLimit").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Forces").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Wind").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Retargetting").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Performance").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Functions").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Alpha").SetSortOrder(SortOrder++);
	}
}


void UAnimGraphNode_AnimDynamics::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

FEditorModeID UAnimGraphNode_AnimDynamics::GetEditorMode() const
{
	return AnimNodeEditModes::AnimDynamics;
}

FText UAnimGraphNode_AnimDynamics::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("ControllerDescription"), GetControllerDescription());
	Arguments.Add(TEXT("BoundBoneName"), FText::FromName(Node.BoundBone.BoneName));
	if(Node.bChain)
	{
		Arguments.Add(TEXT("ChainEndBoneName"), FText::FromName(Node.ChainEnd.BoneName));
	}

	if(TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		if(Node.BoundBone.BoneName == NAME_None || (Node.bChain && Node.ChainEnd.BoneName == NAME_None))
		{
			return GetControllerDescription();
		}

		if(Node.bChain)
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimDynamicsNodeTitleSmallChain", "{ControllerDescription} - Chain: {BoundBoneName} -> {ChainEndBoneName}"), Arguments), this);
		}
		else
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimDynamicsNodeTitleSmall", "{ControllerDescription} - Bone: {BoundBoneName}"), Arguments), this);
		}
	}
	else
	{
		if(Node.bChain)
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimDynamicsNodeTitleLargeChain", "{ControllerDescription}\nChain: {BoundBoneName} -> {ChainEndBoneName}"), Arguments), this);
		}
		else
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimDynamicsNodeTitleLarge", "{ControllerDescription}\nBone: {BoundBoneName}"), Arguments), this);
		}
	}

	return CachedNodeTitles[TitleType];
}

void UAnimGraphNode_AnimDynamics::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FAnimNode_AnimDynamics, ChainEnd))
	{
		// bChain has been modified.
		if (Node.bChain)
		{
			Node.ChainEnd = Node.PhysicsBodyDefinitions[0].BoundBone;
		}
		else
		{
			Node.ChainEnd.BoneName = NAME_None;
		}

		Node.UpdateChainPhysicsBodyDefinitions(LastPreviewComponent);
	}

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName))
	{
		// Either BoundBone or ChainEnd have been modified.
		Node.UpdateChainPhysicsBodyDefinitions(LastPreviewComponent);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UAnimGraphNode_AnimDynamics::PostLoad()
{
	Super::PostLoad();
}

void UAnimGraphNode_AnimDynamics::ResetSim()
{
	FAnimNode_AnimDynamics* PreviewNode = GetPreviewDynamicsNode();
	if(PreviewNode)
	{
		PreviewNode->RequestInitialise(ETeleportType::ResetPhysics);
	}
}

FAnimNode_AnimDynamics* UAnimGraphNode_AnimDynamics::GetPreviewDynamicsNode() const
{
	FAnimNode_AnimDynamics* ActivePreviewNode = nullptr;

	if(LastPreviewComponent && LastPreviewComponent->GetAnimInstance())
	{
		UAnimInstance* Instance = LastPreviewComponent->GetAnimInstance();
		if(UAnimBlueprintGeneratedClass* Class = Cast<UAnimBlueprintGeneratedClass>(Instance->GetClass()))
		{
			ActivePreviewNode = Class->GetPropertyInstance<FAnimNode_AnimDynamics>(Instance, NodeGuid);
		}
	}

	return ActivePreviewNode;
}

FReply UAnimGraphNode_AnimDynamics::ResetButtonClicked(IDetailLayoutBuilder* DetailLayoutBuilder)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjectsList = DetailLayoutBuilder->GetSelectedObjects();
	for(TWeakObjectPtr<UObject> Object : SelectedObjectsList)
	{
		if(UAnimGraphNode_AnimDynamics* AnimDynamicsNode = Cast<UAnimGraphNode_AnimDynamics>(Object.Get()))
		{
			AnimDynamicsNode->ResetSim();
		}
	}
	
	return FReply::Handled();
}

void UAnimGraphNode_AnimDynamics::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FAnimationCustomVersion::GUID);

	const int32 CustomAnimVersion = Ar.CustomVer(FAnimationCustomVersion::GUID);
	
	if(CustomAnimVersion < FAnimationCustomVersion::AnimDynamicsAddAngularOffsets)
	{
		FAnimPhysConstraintSetup& ConSetup = Node.ConstraintSetup_DEPRECATED;
		ConSetup.AngularLimitsMin = FVector(-ConSetup.AngularXAngle_DEPRECATED, -ConSetup.AngularYAngle_DEPRECATED, -ConSetup.AngularZAngle_DEPRECATED);
		ConSetup.AngularLimitsMax = FVector(ConSetup.AngularXAngle_DEPRECATED, ConSetup.AngularYAngle_DEPRECATED, ConSetup.AngularZAngle_DEPRECATED);
	}

	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
		
	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::GravityOverrideDefinedInWorldSpace)
	{
		Node.bGravityOverrideInSimSpace = true;
	}

	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimDynamicsEditableChainParameters)
	{
		// Initialise first physics body using deprecated parameters then re-initialize chain.
		Node.PhysicsBodyDefinitions.Reset();		
		FAnimPhysBodyDefinition PhysBodyDef;
		PhysBodyDef.BoundBone = Node.BoundBone;
		PhysBodyDef.BoxExtents = Node.BoxExtents_DEPRECATED;
		PhysBodyDef.LocalJointOffset = -Node.LocalJointOffset_DEPRECATED; // Note: definition of joint offset has changed from 'Joint position relative to physics body' to 'physics body position relative to Joint'.
		PhysBodyDef.ConstraintSetup = Node.ConstraintSetup_DEPRECATED;
		PhysBodyDef.CollisionType = Node.CollisionType_DEPRECATED;
		PhysBodyDef.SphereCollisionRadius = Node.SphereCollisionRadius_DEPRECATED;
		Node.PhysicsBodyDefinitions.Add(PhysBodyDef);
	}
}

#undef LOCTEXT_NAMESPACE
