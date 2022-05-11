// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNodePalette.h"

#include "Elements/PCGExecuteBlueprint.h"
#include "PCGEditor.h"
#include "PCGEditorGraphSchema.h"
#include "PCGGraph.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "SGraphActionMenu.h"

#define LOCTEXT_NAMESPACE "SPCGEditorGraphNodePalette"

void SPCGEditorGraphNodePaletteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData)
{
	check(InCreateData->Action.IsValid());

	TSharedPtr<FEdGraphSchemaAction> GraphAction = InCreateData->Action;
	ActionPtr = InCreateData->Action;

	bool bIsReadOnly = false;
	TSharedRef<SWidget> NameSlotWidget = CreateTextSlotWidget(InCreateData, bIsReadOnly);

	this->ChildSlot
	[
		NameSlotWidget
	];
}

FText SPCGEditorGraphNodePaletteItem::GetItemTooltip() const
{
	return ActionPtr.Pin()->GetTooltipDescription();
}



void SPCGEditorGraphNodePalette::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SAssignNew(GraphActionMenu, SGraphActionMenu)
		.OnActionDragged(this, &SPCGEditorGraphNodePalette::OnActionDragged)
		.OnCreateWidgetForAction(this, &SPCGEditorGraphNodePalette::OnCreateWidgetForAction)
		.OnCollectAllActions(this, &SPCGEditorGraphNodePalette::CollectAllActions)
		.AutoExpandActionMenu(true)
	];

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SPCGEditorGraphNodePalette::OnAssetChanged);
	AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SPCGEditorGraphNodePalette::OnAssetChanged);
	AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SPCGEditorGraphNodePalette::OnAssetChanged); // Todo, evaluate if this triggers too often
	AssetRegistryModule.Get().OnAssetRenamed().AddSP(this, &SPCGEditorGraphNodePalette::OnAssetRenamed);
}

SPCGEditorGraphNodePalette::~SPCGEditorGraphNodePalette()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().OnAssetAdded().RemoveAll(this);
		AssetRegistryModule.Get().OnAssetRemoved().RemoveAll(this);
		AssetRegistryModule.Get().OnAssetUpdated().RemoveAll(this);
		AssetRegistryModule.Get().OnAssetRenamed().RemoveAll(this);
	}
}

TSharedRef<SWidget> SPCGEditorGraphNodePalette::OnCreateWidgetForAction(FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SPCGEditorGraphNodePaletteItem, InCreateData);
}

void SPCGEditorGraphNodePalette::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	const UPCGEditorGraphSchema* PCGSchema = GetDefault<UPCGEditorGraphSchema>();

	FGraphActionMenuBuilder ActionMenuBuilder;
	PCGSchema->GetPaletteActions(ActionMenuBuilder);
	OutAllActions.Append(ActionMenuBuilder);
}

void SPCGEditorGraphNodePalette::OnAssetChanged(const FAssetData& InAssetData)
{
	FString InNativeParentClassName = InAssetData.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
	FString TargetNativeParentClassName = FString::Printf(TEXT("%s'%s'"), *UClass::StaticClass()->GetName(), *UPCGBlueprintElement::StaticClass()->GetPathName());

	if (InAssetData.AssetClass == UPCGGraph::StaticClass()->GetFName() ||
		(InAssetData.AssetClass == UBlueprint::StaticClass()->GetFName() && InNativeParentClassName == TargetNativeParentClassName))
	{
		RefreshActionsList(true);
	}
}

void SPCGEditorGraphNodePalette::OnAssetRenamed(const FAssetData& InAssetData, const FString& /*InNewAssetName*/)
{
	OnAssetChanged(InAssetData);
}

#undef LOCTEXT_NAMESPACE
 