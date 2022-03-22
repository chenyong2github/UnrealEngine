// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintHeaderView.h"
#include "Widgets/SWidget.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateIcon.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "SBlueprintHeaderView.h"
#include "ContentBrowserModule.h"
#include "Framework/Commands/UIAction.h"
#include "Engine/Blueprint.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "BlueprintHeaderViewApp"

namespace BlueprintHeaderViewModule
{
	static const FName HeaderViewTabName = "BlueprintHeaderViewApp";

	TSharedRef<SDockTab> CreateHeaderViewTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBlueprintHeaderView)
			];
	}
}

void FBlueprintHeaderViewModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BlueprintHeaderViewModule::HeaderViewTabName, FOnSpawnTab::CreateStatic(&BlueprintHeaderViewModule::CreateHeaderViewTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Blueprint Header View"))
		.SetTooltipText(LOCTEXT("TooltipText", "Displays a Blueprint Class in C++ Header format."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Class"));

	SetupContentBrowserContextMenuExtender();
}

void FBlueprintHeaderViewModule::ShutdownModule()
{
	if (ContentBrowserExtenderDelegateHandle.IsValid())
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray< FContentBrowserMenuExtender_SelectedAssets >& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll([ContentBrowserExtenderDelegateHandle=ContentBrowserExtenderDelegateHandle](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
			{
				return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle;
			});
	}
}
	
bool FBlueprintHeaderViewModule::IsClassHeaderViewSupported(const UClass* InClass)
{
	return InClass == UBlueprint::StaticClass();
}

void FBlueprintHeaderViewModule::OpenHeaderViewForAsset(FAssetData InAssetData)
{
	TSharedPtr<SDockTab> HeaderViewTab = FGlobalTabmanager::Get()->TryInvokeTab(BlueprintHeaderViewModule::HeaderViewTabName);

	if (HeaderViewTab.IsValid())
	{
		TSharedRef<SWidget> HeaderViewContentWidget = HeaderViewTab->GetContent();
		if (HeaderViewContentWidget->GetWidgetClass().GetWidgetType() == SBlueprintHeaderView::StaticWidgetClass().GetWidgetType())
		{
			StaticCastSharedRef<SBlueprintHeaderView>(HeaderViewContentWidget)->OnAssetSelected(InAssetData);
		}
	}
}

void FBlueprintHeaderViewModule::SetupContentBrowserContextMenuExtender()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray< FContentBrowserMenuExtender_SelectedAssets >& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FBlueprintHeaderViewModule::OnExtendContentBrowserAssetSelectionMenu));
	ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
}

TSharedRef<FExtender> FBlueprintHeaderViewModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	if (SelectedAssets.Num() == 1)
	{
		if (IsClassHeaderViewSupported(SelectedAssets[0].GetClass()))
		{
			Extender->AddMenuExtension("GetAssetActions", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
				[SelectedAssets](FMenuBuilder& MenuBuilder) {
					MenuBuilder.AddMenuEntry(
						LOCTEXT("OpenHeaderView", "Display in Blueprint Header View"),
						LOCTEXT("OpenHeaderViewTooltip", "Opens this Blueprint in the Blueprint Header View"),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Class"),
						FUIAction(FExecuteAction::CreateStatic(&FBlueprintHeaderViewModule::OpenHeaderViewForAsset, SelectedAssets[0]))
						);
				})
			);
		}
	}

	return Extender;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintHeaderViewModule, BlueprintHeaderView)
