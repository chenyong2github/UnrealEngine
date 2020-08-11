// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "SResetToDefaultMenu.h"
#include "VirtualHeightfieldMeshComponent.h"
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

//	IDetailCategoryBuilder& VirtualTextureCategory = DetailBuilder.EditCategory("Heightmap", FText::GetEmpty());
	
}

#undef LOCTEXT_NAMESPACE
