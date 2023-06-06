// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "EdGraph_PluginReferenceViewer.h"
#include "GraphEditor.h"

//////////////////////////////////////////////////////////////////////////
// SPluginReferenceViewer

class IPlugin;
class FUICommandInfo;
class FUICommandList;
class UEdGraphNode;
class UEdGraph_PluginReferenceViewer;

class SPluginReferenceViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPluginReferenceViewer)
	{}

	SLATE_END_ARGS()

	~SPluginReferenceViewer();

public:
	void Construct(const FArguments& InArgs);

	void SetGraphRoot(const TArray<FPluginIdentifier>& GraphRootIdentifiers);

	int32 GetSearchReferencerDepthCount() const;
	int32 GetSearchDependencyDepthCount() const;
	bool IsCompactModeChecked();
	bool IsShowEnginePluginsChecked() const;
	bool IsShowOptionalPluginsChecked() const;

private:
	void OnOpenPluginProperties();
	bool HasAtLeastOneRealNodeSelected();
	void OpenPluginProperties(const FString& PluginName);

	TSharedRef<SWidget> MakeToolBar();
	TSharedRef<SWidget> GetShowMenuContent();

	void RebuildGraph();
	void ZoomToFit();
	void ReCenterGraph();
	void ReCenterGraphOnNodes(const TSet<UObject*>& Nodes);
	void RegisterActions();

	void OnNodeDoubleClicked(UEdGraphNode* Node);

	void OnCompactModeChanged();
	void OnShowEnginePluginsChanged();
	void OnShowOptionalPluginsChanged();

	struct FPluginReferenceViewerSettings
	{
		int32 MaxSearchReferencersDepth = 1;
		int32 MaxSearchDependencyDepth = 1;
		bool bIsCompactMode = false;
		bool bShowEnginePlugins = true;
		bool bShowOptionalPlugins = true;
	};

private:
	TSharedPtr<SGraphEditor> GraphEditorPtr;
	TSharedPtr<FUICommandList> PluginReferenceViewerActions;
	TSharedPtr<SWidget> ReferencerCountBox;
	TSharedPtr<SWidget> DependencyCountBox;

	UEdGraph_PluginReferenceViewer* GraphObj;
	FPluginReferenceViewerSettings Settings;

	TSharedPtr<FUICommandInfo> ShowEnginePlugins;

	/** Used to delay graph rebuilding during spinbox slider interaction */
	bool bNeedsGraphRebuild;
};