// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_MaterialInstanceDynamic.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_MaterialInstanceDynamic::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	TSharedRef<FSimpleAssetEditor> Editor = FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
	// The editor for MIDs should be read-only as it's not intended to be changed through this method,
	// but it's still helpful to be able to see what the current values on the object are.
	Editor->SetPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateStatic([]() -> bool
	{
		return false;
	}));
}

#undef LOCTEXT_NAMESPACE
