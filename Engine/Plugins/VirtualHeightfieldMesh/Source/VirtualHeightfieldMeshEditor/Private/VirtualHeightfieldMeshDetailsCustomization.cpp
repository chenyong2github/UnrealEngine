// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "SResetToDefaultMenu.h"
#include "VirtualHeightfieldMeshComponent.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "VirtualHeightfieldMeshEditorModule"

FVirtualHeightfieldMeshComponentDetailsCustomization::FVirtualHeightfieldMeshComponentDetailsCustomization()
{
}

TSharedRef<IDetailCustomization> FVirtualHeightfieldMeshComponentDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FVirtualHeightfieldMeshComponentDetailsCustomization);
}

void FVirtualHeightfieldMeshComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Get and store the linked UVirtualHeightfieldMeshComponent
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	VirtualHeightfieldMeshComponent = Cast<UVirtualHeightfieldMeshComponent>(ObjectsBeingCustomized[0].Get());
	if (VirtualHeightfieldMeshComponent == nullptr)
	{
		return;
	}

	IDetailCategoryBuilder& HeightfieldCategory = DetailBuilder.EditCategory("Heightfield", FText::GetEmpty());
	
	HeightfieldCategory
	.AddCustomRow(LOCTEXT("Button_SetBounds", "Set Bounds"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Button_SetBounds", "Set Bounds"))
		.ToolTipText(LOCTEXT("Button_SetBounds_Tooltip", "Copy the bounds from the virtual texture volume."))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.Text(LOCTEXT("Button_SetBounds", "Set Bounds"))
		.OnClicked(this, &FVirtualHeightfieldMeshComponentDetailsCustomization::SetBounds)
	];
}

FReply FVirtualHeightfieldMeshComponentDetailsCustomization::SetBounds()
{
	ARuntimeVirtualTextureVolume* VirtualTextureVolume = VirtualHeightfieldMeshComponent->GetVirtualTextureVolume();
	if (VirtualTextureVolume != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("Transaction_SetBounds", "Set VirtualHeightfieldMeshComponent Bounds"));
		
		AActor* Owner = VirtualHeightfieldMeshComponent->GetOwner();
		Owner->Modify();
		Owner->SetActorTransform(VirtualTextureVolume->GetTransform());
		Owner->PostEditMove(true);
	
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
