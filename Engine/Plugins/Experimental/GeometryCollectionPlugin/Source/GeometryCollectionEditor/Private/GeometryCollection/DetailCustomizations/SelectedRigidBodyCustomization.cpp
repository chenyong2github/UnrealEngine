// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/DetailCustomizations/SelectedRigidBodyCustomization.h"

#include "PropertyHandle.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "EditorFontGlyphs.h"
#include "GeometryCollection/GeometryCollectionDebugDrawActor.h"
#include "GeometryCollection/GeometryCollectionSelectRigidBodyEdMode.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSelectedRigidBodyCustomization, Log, All);

TSharedRef<IPropertyTypeCustomization> FSelectedRigidBodyCustomization::MakeInstance()
{
	return MakeShareable(new FSelectedRigidBodyCustomization);
}

void FSelectedRigidBodyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	const TSharedRef<IPropertyHandle> PropertyHandleId = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Id)).ToSharedRef();
	const TSharedRef<IPropertyHandle> PropertyHandleSolver = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Solver)).ToSharedRef();
	const TSharedRef<IPropertyHandle> PropertyHandleActor = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, GeometryCollection)).ToSharedRef();

	// Add buttons
	HeaderRow.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget(
				NSLOCTEXT("ChaosSelectedRigidBody", "ChaosSelectedRigidBody_Text", "Selected Rigid Body"),
				NSLOCTEXT("ChaosSelectedRigidBody", "ChaosSelectedRigidBody_ToolTip", "Select a Rigid Body here by either entering its Id, or clicking on the Pick button."))
		]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.Padding(0.f)
		.OnMouseDoubleClick_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })  // Disable the double click on all arrows to avoid closing/opening the child properties by mistake
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f)
			[
				SAssignNew(CheckBoxPick, SCheckBox)
				.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_ToolTip", "Pick a Rigid Body."))
				.ForegroundColor(FSlateColor::UseForeground())
				.Padding(6.0f)
				.IsChecked_Lambda([]()
				{
					return FGeometryCollectionSelectRigidBodyEdMode::IsModeActive() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.IsEnabled_Lambda([]()
				{
					return FGeometryCollectionSelectRigidBodyEdMode::CanActivateMode();
				})
				.OnCheckStateChanged(this, &FSelectedRigidBodyCustomization::OnPick, PropertyHandleId, PropertyHandleSolver)
				[ 
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_PickActorInteractive"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_UpClusterLevel", "Go to parent cluster."))
				.ForegroundColor(FSlateColor::UseForeground())
				.IsEnabled_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					return GetParentClusterRigidBodyId(PropertyHandleActor, PropertyHandleId) != INDEX_NONE;
				})
				.OnClicked_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					const int32 RigidBodyId = GetParentClusterRigidBodyId(PropertyHandleActor, PropertyHandleId);
					if (RigidBodyId != INDEX_NONE)
					{
						PropertyHandleId->SetValue(RigidBodyId);
					}
					return FReply::Handled();
				})
				[ 
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
					.Text(FEditorFontGlyphs::Caret_Square_O_Up)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_DownClusterLevel", "Go to child cluster."))
				.ForegroundColor(FSlateColor::UseForeground())
				.IsEnabled_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					return GetChildClusterRigidBodyId(PropertyHandleActor, PropertyHandleId) != INDEX_NONE;
				})
				.OnClicked_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					const int32 RigidBodyId = GetChildClusterRigidBodyId(PropertyHandleActor, PropertyHandleId);
					if (RigidBodyId != INDEX_NONE)
					{
						PropertyHandleId->SetValue(RigidBodyId);
					}
					return FReply::Handled();
				})
				[ 
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
					.Text(FEditorFontGlyphs::Caret_Square_O_Down)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_PrevClusterSibling", "Go to previous clustered sibling."))
				.ForegroundColor(FSlateColor::UseForeground())
				.IsEnabled_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					return GetPreviousClusteredSiblingRigidBodyId(PropertyHandleActor, PropertyHandleId) != INDEX_NONE;
				})
				.OnClicked_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					const int32 RigidBodyId = GetPreviousClusteredSiblingRigidBodyId(PropertyHandleActor, PropertyHandleId);
					if (RigidBodyId != INDEX_NONE)
					{
						PropertyHandleId->SetValue(RigidBodyId);
					}
					return FReply::Handled();
				})
				[ 
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
					.Text(FEditorFontGlyphs::Caret_Square_O_Left)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_NextClusterSibling", "Go to next clustered sibling."))
				.ForegroundColor(FSlateColor::UseForeground())
				.IsEnabled_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					return GetNextClusteredSiblingRigidBodyId(PropertyHandleActor, PropertyHandleId) != INDEX_NONE;
				})
				.OnClicked_Lambda([PropertyHandleActor, PropertyHandleId]()
				{
					const int32 RigidBodyId = GetNextClusteredSiblingRigidBodyId(PropertyHandleActor, PropertyHandleId);
					if (RigidBodyId != INDEX_NONE)
					{
						PropertyHandleId->SetValue(RigidBodyId);
					}
					return FReply::Handled();
				})
				[ 
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
					.Text(FEditorFontGlyphs::Caret_Square_O_Right)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
	];
}

void FSelectedRigidBodyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	const TSharedRef<IPropertyHandle> PropertyHandleId = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Id)).ToSharedRef();
	const TSharedRef<IPropertyHandle> PropertyHandleSolver = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Solver)).ToSharedRef();
	const TSharedRef<IPropertyHandle> PropertyHandleGeometryCollection = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, GeometryCollection)).ToSharedRef();

	ChildBuilder.AddProperty(PropertyHandleId);
	ChildBuilder.AddProperty(PropertyHandleSolver);
	ChildBuilder.AddProperty(PropertyHandleGeometryCollection);
}

void FSelectedRigidBodyCustomization::GetSelectedGeometryCollectionCluster(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId, const UGeometryCollectionComponent*& OutGeometryCollectionComponent, int32& OutTransformIndex)
{
	OutGeometryCollectionComponent = nullptr;
	OutTransformIndex = INDEX_NONE;

	// Retrieve selected rigid body
	int32 SelectedRigidBodyId;
	if (PropertyHandleId->GetValue(SelectedRigidBodyId) == FPropertyAccess::Success && SelectedRigidBodyId != INDEX_NONE)
	{
		// Retrieve selected Geometry Collection Actor
		UObject* Object;
		if (PropertyHandleActor->GetValue(Object) == FPropertyAccess::Success && Object != nullptr)
		{
			// Retrieve selected tranform index
			const UGeometryCollectionComponent* const GeometryCollectionComponent = CastChecked<AGeometryCollectionActor>(Object)->GetGeometryCollectionComponent();
			const TManagedArray<int32>& RigidBodyIds = GeometryCollectionComponent->GetRigidBodyIdArray();

			for (int32 TransformIndex = 0; TransformIndex < RigidBodyIds.Num(); ++TransformIndex)
			{
				const int32 RigidBodyId = RigidBodyIds[TransformIndex];
				if (RigidBodyId == SelectedRigidBodyId)
				{
					OutGeometryCollectionComponent = GeometryCollectionComponent;
					OutTransformIndex = TransformIndex;
					break;
				}
			}
		}
	}
	UE_CLOG(OutGeometryCollectionComponent && OutGeometryCollectionComponent->GetOwner(), LogSelectedRigidBodyCustomization, VeryVerbose, TEXT("Component actor %s, TransformIndex %d."), *OutGeometryCollectionComponent->GetOwner()->GetName(), OutTransformIndex);
}

int32 FSelectedRigidBodyCustomization::GetParentClusterRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId)
{
	const UGeometryCollectionComponent* GeometryCollectionComponent;
	int32 TransformIndex;
	GetSelectedGeometryCollectionCluster(PropertyHandleActor, PropertyHandleId, GeometryCollectionComponent, TransformIndex);
	if (GeometryCollectionComponent)
	{
		const int32 ParentTransformIndex = GeometryCollectionComponent->GetParentArray()[TransformIndex];
		if (ParentTransformIndex != INDEX_NONE)
		{
			return GeometryCollectionComponent->GetRigidBodyIdArray()[ParentTransformIndex];
		}
	}
	return INDEX_NONE;
}

int32 FSelectedRigidBodyCustomization::GetChildClusterRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId)
{
	const UGeometryCollectionComponent* GeometryCollectionComponent;
	int32 TransformIndex;
	GetSelectedGeometryCollectionCluster(PropertyHandleActor, PropertyHandleId, GeometryCollectionComponent, TransformIndex);
	if (GeometryCollectionComponent)
	{
		const TSet<int32>& Children = GeometryCollectionComponent->GetChildrenArray()[TransformIndex];
		// Iterate through all children until the first valid rigid body id is found
		for (TSet<int32>::TConstIterator ChildTransformIndexIt = Children.CreateConstIterator(); ChildTransformIndexIt; ++ChildTransformIndexIt)
		{
			const int32 RigidBodyId = GeometryCollectionComponent->GetRigidBodyIdArray()[*ChildTransformIndexIt];
			if (RigidBodyId != INDEX_NONE)
			{
				return RigidBodyId;
			}
		}
	}
	return INDEX_NONE;
}

int32 FSelectedRigidBodyCustomization::GetPreviousClusteredSiblingRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId)
{
	const UGeometryCollectionComponent* GeometryCollectionComponent;
	int32 TransformIndex;
	GetSelectedGeometryCollectionCluster(PropertyHandleActor, PropertyHandleId, GeometryCollectionComponent, TransformIndex);
	if (GeometryCollectionComponent)
	{
		const int32 ParentTransformIndex = GeometryCollectionComponent->GetParentArray()[TransformIndex];
		if (ParentTransformIndex != INDEX_NONE)
		{
			const TSet<int32>& Siblings = GeometryCollectionComponent->GetChildrenArray()[ParentTransformIndex];
			int32 PrevRigidBodyId = INDEX_NONE;
			for (int32 Sibling : Siblings)
			{
				if (Sibling == TransformIndex)
				{
					return PrevRigidBodyId;
				}
				// Only use a previous sibling with a valid rigid body id
				const int32 RigidBodyId = GeometryCollectionComponent->GetRigidBodyIdArray()[Sibling];
				if (RigidBodyId != INDEX_NONE)
				{
					PrevRigidBodyId = RigidBodyId;
				}
			}
		}
	}
	return INDEX_NONE;
}

int32 FSelectedRigidBodyCustomization::GetNextClusteredSiblingRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId)
{
	const UGeometryCollectionComponent* GeometryCollectionComponent;
	int32 TransformIndex;
	GetSelectedGeometryCollectionCluster(PropertyHandleActor, PropertyHandleId, GeometryCollectionComponent, TransformIndex);
	if (GeometryCollectionComponent)
	{
		const int32 ParentTransformIndex = GeometryCollectionComponent->GetParentArray()[TransformIndex];
		if (ParentTransformIndex != INDEX_NONE)
		{
			const TSet<int32>& Siblings = GeometryCollectionComponent->GetChildrenArray()[ParentTransformIndex];
			int32 Sibling = INDEX_NONE;
			for (int32 NextSibling : Siblings)
			{
				if (Sibling == TransformIndex)
				{
					// Return the first valid rigid body id
					const int32 NextRigidBodyId = GeometryCollectionComponent->GetRigidBodyIdArray()[NextSibling];
					if (NextRigidBodyId != INDEX_NONE)
					{
						return NextRigidBodyId;
					}
				}
				else
				{
					Sibling = NextSibling;
				}
			}
		}
	}
	return INDEX_NONE;
}

void FSelectedRigidBodyCustomization::OnPick(ECheckBoxState InCheckState, TSharedRef<IPropertyHandle> PropertyHandleId, TSharedRef<IPropertyHandle> PropertyHandleSolver) const
{
	// Set unchecked by default (enter mode is required to set checked)
	if (CheckBoxPick)
	{
		CheckBoxPick->SetIsChecked(ECheckBoxState::Unchecked);
	}

	if (InCheckState == ECheckBoxState::Unchecked)
	{
		if (FGeometryCollectionSelectRigidBodyEdMode::IsModeActive())
		{
			FGeometryCollectionSelectRigidBodyEdMode::DeactivateMode();
		}
	}
	else
	{
		const TWeakPtr<SCheckBox> CheckBoxPickWeakPtr = CheckBoxPick;
		const TFunction<void()> OnEnterMode = [CheckBoxPickWeakPtr]()
		{
			if (const TSharedPtr<SCheckBox> CheckBoxPickPin = CheckBoxPickWeakPtr.Pin())
			{
				CheckBoxPickPin->SetToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "CancelPick_ToolTip", "Cancel Picking."));
				CheckBoxPickPin->SetIsChecked(ECheckBoxState::Checked);
			}
		};
		const TFunction<void()> OnExitMode = [CheckBoxPickWeakPtr]()
		{
			if (const TSharedPtr<SCheckBox> CheckBoxPickPin = CheckBoxPickWeakPtr.Pin())
			{
				CheckBoxPickPin->SetToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_ToolTip", "Pick a Rigid Body."));
				CheckBoxPickPin->SetIsChecked(ECheckBoxState::Unchecked);
			}
		};
		FGeometryCollectionSelectRigidBodyEdMode::ActivateMode(PropertyHandleId, PropertyHandleSolver, OnEnterMode, OnExitMode);
	}
}
