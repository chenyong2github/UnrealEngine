// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_RigidBody.h"
#include "AnimNodeEditModes.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "IPhysicsAssetRenderInterface.h"

// Details includes
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_RigidBody

#define LOCTEXT_NAMESPACE "RigidBody"

UAnimGraphNode_RigidBody::UAnimGraphNode_RigidBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_RigidBody::GetControllerDescription() const
{
	return LOCTEXT("AnimGraphNode_RigidBody_ControllerDescription", "Rigid body simulation for physics asset");
}

FText UAnimGraphNode_RigidBody::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_RigidBody_Tooltip", "This simulates based on the skeletal mesh component's physics asset");
}

FText UAnimGraphNode_RigidBody::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText(LOCTEXT("AnimGraphNode_RigidBody_NodeTitle", "RigidBody"));
}

void UAnimGraphNode_RigidBody::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
#if !WITH_CHAOS
	if(Node.bEnableWorldGeometry && Node.SimulationSpace != ESimulationSpace::WorldSpace)
	{
		MessageLog.Error(*LOCTEXT("AnimGraphNode_CompileError", "@@ - uses world collision without world space simulation. This is not supported").ToString());
	}
#endif
	
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

void UAnimGraphNode_RigidBody::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const
{
	if (const FAnimNode_RigidBody* const RuntimeRigidBodyNode = GetDebuggedAnimNode<FAnimNode_RigidBody>())
	{
		if (UPhysicsAsset* const PhysicsAsset = RuntimeRigidBodyNode->GetPhysicsAsset())
		{
			IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");
			PhysicsAssetRenderInterface.DebugDraw(PreviewSkelMeshComp, PhysicsAsset, PDI);
		}
	}
}

void UAnimGraphNode_RigidBody::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(TEXT("Debug Visualization"));
	FDetailWidgetRow& WidgetRow = ViewportCategory.AddCustomRow(LOCTEXT("ToggleDebugVisualizationButtonRow", "DebugVisualization"));
	FAnimNode_RigidBody* const RigidBodyNode = static_cast<FAnimNode_RigidBody*>(GetDebuggedAnimNode());

	WidgetRow
		[
			SNew(SHorizontalBox)
			// Show/Hide Bodies button.
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([this](){ this->ToggleBodyVisibility(); return FReply::Handled(); })
				.ButtonColorAndOpacity_Lambda([this](){ return (AreAnyBodiesHidden()) ? FAppStyle::Get().GetSlateColor("Colors.AccentRed") : FAppStyle::Get().GetSlateColor("Colors.AccentGreen"); })
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return (AreAnyBodiesHidden()) ? LOCTEXT("ShowAllBodiesButtonText", "Show All Bodies") : LOCTEXT("HideAllBodiesButtonText", "Hide All Bodies"); })
					.ToolTipText(LOCTEXT("ToggleBodyVisibilityButtonToolTip", "Toggle debug visualization of all physics bodies"))
				]
			]
			// Show/Hide Constraints button.
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([this](){ this->ToggleConstraintVisibility(); return FReply::Handled(); })
				.ButtonColorAndOpacity_Lambda([this]() { return (AreAnyConstraintsHidden()) ? FAppStyle::Get().GetSlateColor("Colors.AccentRed") : FAppStyle::Get().GetSlateColor("Colors.AccentGreen"); })
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return (AreAnyConstraintsHidden()) ? LOCTEXT("ShowAllConstraintsButtonText", "Show All Constraints") : LOCTEXT("HideAllConstraintsButtonText", "Hide All Constraints"); })
					.ToolTipText(LOCTEXT("ToggleConstraintVisibilityButtonToolTip", "Toggle debug visualization of all physics constriants"))
				]
			]
		];
}

void UAnimGraphNode_RigidBody::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");
	PhysicsAssetRenderInterface.SaveConfig();
}

void UAnimGraphNode_RigidBody::ToggleBodyVisibility()
{
	FAnimNode_RigidBody* const RigidBodyNode = static_cast<FAnimNode_RigidBody*>(GetDebuggedAnimNode());
	
	IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");

	if (RigidBodyNode)
	{
		PhysicsAssetRenderInterface.ToggleShowAllBodies(RigidBodyNode->GetPhysicsAsset());
	}
}

void UAnimGraphNode_RigidBody::ToggleConstraintVisibility()
{
	FAnimNode_RigidBody* const RigidBodyNode = static_cast<FAnimNode_RigidBody*>(GetDebuggedAnimNode());
	IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");

	if (RigidBodyNode)
	{
		PhysicsAssetRenderInterface.ToggleShowAllConstraints(RigidBodyNode->GetPhysicsAsset());
	}
}

bool UAnimGraphNode_RigidBody::AreAnyBodiesHidden() const
{
	FAnimNode_RigidBody* const RigidBodyNode = static_cast<FAnimNode_RigidBody*>(GetDebuggedAnimNode());
	IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");

	if (RigidBodyNode)
	{
		return PhysicsAssetRenderInterface.AreAnyBodiesHidden(RigidBodyNode->GetPhysicsAsset());
	}

	return false;
}

bool UAnimGraphNode_RigidBody::AreAnyConstraintsHidden() const
{
	FAnimNode_RigidBody* const RigidBodyNode = static_cast<FAnimNode_RigidBody*>(GetDebuggedAnimNode());
	IPhysicsAssetRenderInterface& PhysicsAssetRenderInterface = IModularFeatures::Get().GetModularFeature<IPhysicsAssetRenderInterface>("PhysicsAssetRenderInterface");

	if (RigidBodyNode)
	{
		return PhysicsAssetRenderInterface.AreAnyConstraintsHidden(RigidBodyNode->GetPhysicsAsset());
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
