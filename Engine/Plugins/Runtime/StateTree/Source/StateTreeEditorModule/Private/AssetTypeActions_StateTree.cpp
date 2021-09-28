// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_StateTree.h"
#include "StateTree.h"
#include "IStateTreeEditor.h"
#include "StateTreeEditorModule.h"
#include "AIModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_StateTree::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UStateTree* StateTree = Cast<UStateTree>(*ObjIt))
		{
			FStateTreeEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FStateTreeEditorModule>("StateTreeEditorModule");
			TSharedRef<IStateTreeEditor> NewEditor = EditorModule.CreateStateTreeEditor(EToolkitMode::Standalone, EditWithinLevelEditor, StateTree);
		}
	}
}

UClass* FAssetTypeActions_StateTree::GetSupportedClass() const
{
	return UStateTree::StaticClass();
}

uint32 FAssetTypeActions_StateTree::GetCategories()
{ 
	IAIModule& AIModule = FModuleManager::GetModuleChecked<IAIModule>("AIModule").Get();
	return AIModule.GetAIAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
