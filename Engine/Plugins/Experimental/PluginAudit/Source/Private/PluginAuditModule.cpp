// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "SPluginAuditBrowser.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_PluginReference.h"
#include "SPluginReferenceNode.h"

#define LOCTEXT_NAMESPACE "PluginAudit"

class FPluginAuditGraphPanelNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
	{
		if (UEdGraphNode_PluginReference* DependencyNode = Cast<UEdGraphNode_PluginReference>(Node))
		{
			return SNew(SPluginReferenceNode, DependencyNode);
		}

		return nullptr;
	}
};


class FPluginAuditModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End of IModuleInterface interface

private:
	static const FName PluginAuditTabName;
	TSharedRef<SDockTab> SpawnPluginAuditTab(const FSpawnTabArgs& Args);

	TSharedPtr<FPluginAuditGraphPanelNodeFactory> PluginAuditGraphPanelNodeFactory;
};

const FName FPluginAuditModule::PluginAuditTabName = TEXT("PluginAudit");

///////////////////////////////////////////

IMPLEMENT_MODULE(FPluginAuditModule, PluginAudit);


void FPluginAuditModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PluginAuditTabName, FOnSpawnTab::CreateRaw(this, &FPluginAuditModule::SpawnPluginAuditTab))
		.SetDisplayName(LOCTEXT("PluginAuditTitle", "Plugin Audit"))
		.SetTooltipText(LOCTEXT("PluginAuditTooltip", "Open Plugin Audit window, allows viewing detailed information about plugin references."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsAuditCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Audit"));
	FGlobalTabmanager::Get()->RegisterDefaultTabWindowSize(PluginAuditTabName, FVector2D(1080, 600));

	PluginAuditGraphPanelNodeFactory = MakeShareable(new FPluginAuditGraphPanelNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(PluginAuditGraphPanelNodeFactory);
}

void FPluginAuditModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PluginAuditTabName);
}

TSharedRef<SDockTab> FPluginAuditModule::SpawnPluginAuditTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SPluginAuditBrowser)
		];
}

#undef LOCTEXT_NAMESPACE
