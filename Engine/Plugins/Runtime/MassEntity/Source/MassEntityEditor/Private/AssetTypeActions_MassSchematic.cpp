// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_MassSchematic.h"
#include "MassSchematic.h"
#include "IMassEntityEditor.h"
#include "MassEntityEditorModule.h"
#include "AIModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_MassSchematic::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UMassSchematic* Pipe = Cast<UMassSchematic>(*ObjIt))
		{
			FPipeEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FPipeEditorModule>("PipeEditor");
			TSharedRef<IPipeEditor> NewEditor = EditorModule.CreatePipeEditor(Mode, EditWithinLevelEditor, Pipe);
		}
	}
}

UClass* FAssetTypeActions_MassSchematic::GetSupportedClass() const
{
	return UMassSchematic::StaticClass();
}

uint32 FAssetTypeActions_MassSchematic::GetCategories()
{ 
	// @todo will probably need to use a different category, like "gameplay"
	IAIModule& AIModule = FModuleManager::GetModuleChecked<IAIModule>("AIModule").Get();
	return AIModule.GetAIAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
