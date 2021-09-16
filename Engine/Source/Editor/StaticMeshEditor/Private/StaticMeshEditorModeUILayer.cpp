// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshEditorModeUILayer.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Toolkits/IToolkit.h"

FStaticMeshEditorModeUILayer::FStaticMeshEditorModeUILayer(const IToolkitHost* InToolkitHost) :
	FAssetEditorModeUILayer(InToolkitHost)
{}

void FStaticMeshEditorModeUILayer::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	if (!Toolkit->IsAssetEditor())
	{
		FAssetEditorModeUILayer::OnToolkitHostingStarted(Toolkit);
		HostedToolkit = Toolkit;
		Toolkit->SetModeUILayer(SharedThis(this));
		Toolkit->RegisterTabSpawners(ToolkitHost->GetTabManager().ToSharedRef());
		RegisterModeTabSpawners();
		OnToolkitHostReadyForUI.ExecuteIfBound();
	}
}

void FStaticMeshEditorModeUILayer::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	if (HostedToolkit.IsValid() && HostedToolkit.Pin() == Toolkit)
	{
		FAssetEditorModeUILayer::OnToolkitHostingFinished(Toolkit);
	}
}

TSharedPtr<FWorkspaceItem> FStaticMeshEditorModeUILayer::GetModeMenuCategory() const
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	return MenuStructure.GetLevelEditorModesCategory();
}

