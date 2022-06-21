// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNodePalette.h"

#include "Elements/PCGExecuteBlueprint.h"
#include "PCGEditor.h"
#include "PCGEditorGraphSchema.h"
#include "PCGGraph.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "SGraphActionMenu.h"
#include "PCGEditorUtils.h"

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
		IAssetRegistry* AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).TryGet();
		if (AssetRegistry) // AssetRegistry can be null during EngineShutdown even if module is available
		{
			AssetRegistry->OnAssetAdded().RemoveAll(this);
			AssetRegistry->OnAssetRemoved().RemoveAll(this);
			AssetRegistry->OnAssetUpdated().RemoveAll(this);
			AssetRegistry->OnAssetRenamed().RemoveAll(this);
		}
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
	if (InAssetData.AssetClassPath == UPCGGraph::StaticClass()->GetClassPathName() || PCGEditorUtils::IsAssetPCGBlueprint(InAssetData))
	{
		RefreshActionsList(true);
	}
}

void SPCGEditorGraphNodePalette::OnAssetRenamed(const FAssetData& InAssetData, const FString& /*InNewAssetName*/)
{
	OnAssetChanged(InAssetData);
}

#undef LOCTEXT_NAMESPACE
 