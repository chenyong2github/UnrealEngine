// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/DetailCustomizations/SelectedRigidBodyCustomization.h"

#include "PropertyHandle.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "EditorFontGlyphs.h"
#include "GeometryCollection/GeometryCollectionDebugDrawActor.h"
#include "GeometryCollection/GeometryCollectionSelectRigidBodyEdMode.h"

TSharedRef<IPropertyTypeCustomization> FSelectedRigidBodyCustomization::MakeInstance()
{
	return MakeShareable(new FSelectedRigidBodyCustomization);
}

void FSelectedRigidBodyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	const TSharedRef<IPropertyHandle> PropertyHandleId = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Id)).ToSharedRef();

	// Add buttons
	HeaderRow.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget(
				NSLOCTEXT("ChaosSelectedRigidBody", "ChaosSelectedRigidBody_Text", "Selected Rigid Body"),
				NSLOCTEXT("ChaosSelectedRigidBody", "ChaosSelectedRigidBody_ToolTip", "Select a Rigid Body here by either entering its Id, or clicking on the Pick button."))
		]
	.ValueContent()
		.MinDesiredWidth(125.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(80.0f)
			[
				PropertyHandleId->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			.FillWidth(20.0f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(NSLOCTEXT("ChaosSelectedRigidBody", "Pick_ToolTip", "Pick a Rigid Body."))
				.ForegroundColor(FSlateColor::UseForeground())
				.ContentPadding(4.0f)
				.OnClicked(this, &FSelectedRigidBodyCustomization::OnPick, PropertyHandleId)
				[ 
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_PickActorInteractive"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

void FSelectedRigidBodyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/, class IDetailChildrenBuilder& /*ChildBuilder*/, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
}

FReply FSelectedRigidBodyCustomization::OnPick(TSharedRef<IPropertyHandle> PropertyHandleId) const
{
	if (FGeometryCollectionSelectRigidBodyEdMode::IsModeActive())
	{
		FGeometryCollectionSelectRigidBodyEdMode::DeactivateMode();
	}
	else
	{
		FGeometryCollectionSelectRigidBodyEdMode::ActivateMode(PropertyHandleId);
	}
	return FReply::Handled();
}
