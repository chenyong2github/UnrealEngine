// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectAssetTypeActions.h"

#include "SmartObjectAssetEditor.h"
#include "SmartObjectDefinition.h"

const FName DisplaySmartObjectEditorName("SmartObjectEditor");

FAssetTypeActions_SmartObject::FAssetTypeActions_SmartObject(const EAssetTypeCategories::Type InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FAssetTypeActions_SmartObject::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "FAssetTypeActions_SmartObject", "SmartObject Definition");
}

FColor FAssetTypeActions_SmartObject::GetTypeColor() const
{
	return FColor(104,49,178);
}

UClass* FAssetTypeActions_SmartObject::GetSupportedClass() const
{
	return USmartObjectDefinition::StaticClass();
}

uint32 FAssetTypeActions_SmartObject::GetCategories()
{
	return AssetCategory;
}

void FAssetTypeActions_SmartObject::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (USmartObjectDefinition* Asset = Cast<USmartObjectDefinition>(*ObjIt))
		{
			USmartObjectAssetEditor* AssetEditor = NewObject<USmartObjectAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
			AssetEditor->SetObjectToEdit(Asset);
			AssetEditor->Initialize();
		}
	}
}
