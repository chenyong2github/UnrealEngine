// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphAssetActions.h"

#include "EvalGraph/EvalGraphObject.h"

#define LOCTEXT_NAMESPACE "AssetActions_EvalGraphAsset"

FText FEvalGraphAssetActions::GetName() const
{
	return LOCTEXT("Name", "Evalulation Graph");
}

UClass* FEvalGraphAssetActions::GetSupportedClass() const
{
	return UEvalGraph::StaticClass();
}

FColor FEvalGraphAssetActions::GetTypeColor() const
{
	return FColor(255, 127, 40);
}

void FEvalGraphAssetActions::GetActions(const TArray<UObject*>& InObjects,
										class FMenuBuilder&     MenuBuilder)
{
}

void FEvalGraphAssetActions::OpenAssetEditor(
	const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor /*= TSharedPtr<IToolkitHost>()*/)
{
	FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}

uint32 FEvalGraphAssetActions::GetCategories()
{
	return EAssetTypeCategories::Physics;
}

FText FEvalGraphAssetActions::GetAssetDescription(const struct FAssetData& AssetData) const
{
	return LOCTEXT("Description", "An evulation graph for asset authoring.");
}

#undef LOCTEXT_NAMESPACE
