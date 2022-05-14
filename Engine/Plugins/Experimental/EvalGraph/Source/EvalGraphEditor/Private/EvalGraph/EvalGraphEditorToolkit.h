// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "Misc/NotifyHook.h"

class FEditorViewportTabContent;
class IDetailsView;
class FTabManager;
class IToolkitHost;
class UEvalGraph;
class SEvalGraphEditorViewport;

class FEvalGraphEditorToolkit : public FAssetEditorToolkit, public FNotifyHook, public FGCObject
{
public:
	void InitEvalGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit);

	// IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override; 

	// Tab spawners 
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	TSharedRef<SDockTab> SpawnTab_GraphCanvas(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

	// Graph Editor Operations
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);
	void EvaluateNode();
	FGraphPanelSelectionSet GetSelectedNodes() const;

	UEvalGraph* GetEvalGraph() { return EvalGraph; }
	const UEvalGraph* GetEvalGraph() const { return EvalGraph; }

private:

	static const FName GraphCanvasTabId;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedRef<SGraphEditor> CreateGraphEditorWidget(UEvalGraph* ObjectToEdit);

	static const FName PropertiesTabId;
	TSharedPtr<IDetailsView> PropertiesEditor;
	TSharedPtr<IDetailsView> CreatePropertiesEditorWidget(UObject* ObjectToEdit);


	UEvalGraph* EvalGraph = nullptr;

	/** Command list for the graph editor */
	TSharedPtr<FUICommandList> GraphEditorCommands;
};
