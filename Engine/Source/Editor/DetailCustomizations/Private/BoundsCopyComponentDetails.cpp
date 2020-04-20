// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoundsCopyComponentDetails.h"

#include "Components/BoundsCopyComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "BoundsCopyComponentDetails"

FBoundsCopyComponentDetailsCustomization::FBoundsCopyComponentDetailsCustomization()
{
}

TSharedRef<IDetailCustomization> FBoundsCopyComponentDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FBoundsCopyComponentDetailsCustomization);
}

void FBoundsCopyComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Get and store the linked UBoundsCopyComponent
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	BoundsCopyComponent = Cast<UBoundsCopyComponent>(ObjectsBeingCustomized[0].Get());
	if (BoundsCopyComponent == nullptr)
	{
		return;
	}

	// Only reason for having any of the logic here is that CallInEditor doesn't seem to work to add buttons for Copy functions.
	IDetailCategoryBuilder& BoundsCategory = DetailBuilder.EditCategory("TransformFromBounds", FText::GetEmpty(), ECategoryPriority::Important);

	// Hide and re-add BoundsSourceActor property otherwise we lose the ordering of this property first.
	TSharedPtr<IPropertyHandle> SourceActorValue = DetailBuilder.GetProperty("BoundsSourceActor");
	DetailBuilder.HideProperty(SourceActorValue);

	BoundsCategory.AddCustomRow(SourceActorValue->GetPropertyDisplayName())
	.NameContent()
	[
		SourceActorValue->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SourceActorValue->CreatePropertyValueWidget()
	];

	// Add Copy buttons.
	BoundsCategory
	.AddCustomRow(LOCTEXT("Button_CopyRotation", "Copy Rotation"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Button_CopyRotation", "Copy Rotation"))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.Text(LOCTEXT("Button_Copy", "Copy"))
		.ToolTipText(LOCTEXT("Button_CopyRotation_Tooltip", "Set the virtual texture rotation to match the source actor"))
		.OnClicked(this, &FBoundsCopyComponentDetailsCustomization::SetRotation)
	];

	BoundsCategory
	.AddCustomRow(LOCTEXT("Button_CopyBounds", "Copy Bounds"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Button_CopyBounds", "Copy Bounds"))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.Text(LOCTEXT("Button_Copy", "Copy"))
		.ToolTipText(LOCTEXT("Button_CopyBounds_Tooltip", "Set the virtual texture transform so that it includes the full bounds of the source actor"))
		.OnClicked(this, &FBoundsCopyComponentDetailsCustomization::SetTransformToBounds)
	];
}

FReply FBoundsCopyComponentDetailsCustomization::SetRotation()
{
	FScopedTransaction BakeTransaction(LOCTEXT("Transaction_CopyRotation", "Copy Rotation"));
	BoundsCopyComponent->SetRotation();
	return FReply::Handled();
}

FReply FBoundsCopyComponentDetailsCustomization::SetTransformToBounds()
{
	FScopedTransaction BakeTransaction(LOCTEXT("Transaction_CopyBounds", "Copy Bounds"));
	BoundsCopyComponent->SetTransformToBounds();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
