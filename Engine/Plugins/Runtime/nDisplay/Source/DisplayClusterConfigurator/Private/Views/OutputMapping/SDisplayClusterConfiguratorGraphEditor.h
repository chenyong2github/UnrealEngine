// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GraphEditor.h"
#include "NodeFactory.h"
#include "UObject/StrongObjectPtr.h"

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"

class FDisplayClusterConfiguratorViewOutputMapping;
class FDisplayClusterConfiguratorToolkit;
class FMenuBuilder;
class FUICommandList;
class SDisplayClusterConfiguratorGraphEditor;
class SDisplayClusterConfiguratorCanvasNode;
class SGraphNode;
class UDisplayClusterConfiguratorGraph;
class UEdGraph;
class UEdGraphNode;
class UTexture;

struct FActionMenuContent;

enum class ENodeAlignment : uint8
{
	Top,
	Middle,
	Bottom,
	Left,
	Center,
	Right
};

class SDisplayClusterConfiguratorGraphEditor
	: public SGraphEditor
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorGraphEditor)
		: _GraphToEdit(nullptr)
	{}

		SLATE_ARGUMENT(UEdGraph*, GraphToEdit)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		const TSharedRef<FDisplayClusterConfiguratorViewOutputMapping>& InViewOutputMapping);

	void SetViewportPreviewTexture(const FString& NodeId, const FString& ViewportId, UTexture* InTexture);

private:
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);
	void OnObjectSelected();
	void OnConfigReloaded();
	void RebuildCanvasNode();

	/** Callback to create contextual menu for graph nodes in graph panel */
	FActionMenuContent OnCreateNodeOrPinMenu(UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, FMenuBuilder* MenuBuilder, bool bIsDebugging);
	void BindCommands();

	void BrowseDocumentation();

	void DeleteSelectedNodes();
	bool CanDeleteNodes() const;

	void CopySelectedNodes();
	bool CanCopyNodes() const;

	void CutSelectedNodes();
	bool CanCutNodes() const;

	void PasteNodes();
	bool CanPasteNodes() const;

	void DuplicateNodes();
	bool CanDuplicateNodes() const;

	bool CanAlignNodes() const;
	void AlignNodes(ENodeAlignment Alignment);

	void ForEachGraphNode(TFunction<void(UDisplayClusterConfiguratorBaseNode* Node)> Predicate);

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakObjectPtr<UDisplayClusterConfiguratorGraph> ClusterConfiguratorGraph;

	TWeakPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMappingPtr;

	TSet<UObject*> SelectedNodes;

	TStrongObjectPtr<UDisplayClusterConfiguratorCanvasNode> RootCanvasNode;

	bool bClearSelection;

	/** The nodes menu command list */
	TSharedPtr<FUICommandList> CommandList;
};