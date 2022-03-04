// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphAssetTypeActions.h"
#include "PCGGraph.h"
#include "PCGEditor.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<bool> CVarPCGUseGraphEditor(TEXT("pcg.UseGraphEditor"), false, TEXT("Wheter to use use the new graph editor or not."));

FText FPCGGraphAssetTypeActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "PCGGraphAssetTypeActions", "PCG Graph");
}

UClass* FPCGGraphAssetTypeActions::GetSupportedClass() const
{
	return UPCGGraph::StaticClass();
}

void FPCGGraphAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	if (CVarPCGUseGraphEditor.GetValueOnAnyThread())
	{
		for (UObject* Object : InObjects)
		{
			if (UPCGGraph* PCGGraph = Cast<UPCGGraph>(Object))
			{
				TSharedRef<FPCGEditor> PCGEditor(new FPCGEditor());
				PCGEditor->Initialize(EToolkitMode::Standalone, EditWithinLevelEditor, PCGGraph);
			}
		}
	}
	else
	{
		FPCGCommonAssetTypeActions::OpenAssetEditor(InObjects, EditWithinLevelEditor);
	}
}