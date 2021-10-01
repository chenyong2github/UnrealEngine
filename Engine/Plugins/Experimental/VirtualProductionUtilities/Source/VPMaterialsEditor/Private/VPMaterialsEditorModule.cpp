// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPMaterialsEditorModule.h"
#include "SMaterialDynamicWidgets.h"

#include "MaterialList.h"

void FVPMaterialsEditorModule::StartupModule()
{
	// Add bottom extender for material item
	FMaterialList::OnAddMaterialItemViewExtraBottomWidget.AddLambda([](const TSharedRef<FMaterialItemView>& InMaterialItemView, UActorComponent* InCurrentComponent, IDetailLayoutBuilder& InDetailBuilder, TArray<TSharedPtr<SWidget>>& OutExtensions)
	{
		OutExtensions.Add(SNew(SMaterialDynamicView, InMaterialItemView, InCurrentComponent));
	});
}

void FVPMaterialsEditorModule::ShutdownModule()
{
	FMaterialList::OnAddMaterialItemViewExtraBottomWidget.RemoveAll(this);
}

IMPLEMENT_MODULE(FVPMaterialsEditorModule, VPMaterialsEditor)

			