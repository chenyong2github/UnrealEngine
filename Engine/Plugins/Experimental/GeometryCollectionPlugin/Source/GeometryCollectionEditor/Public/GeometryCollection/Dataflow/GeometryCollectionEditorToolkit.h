// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GraphEditor.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "Misc/NotifyHook.h"

class IDetailsView;
class FTabManager;
class IToolkitHost;
class UDataflow;

namespace Dataflow
{
	class GEOMETRYCOLLECTIONEDITOR_API FGeometryCollectionContext : public FContext
	{
	public:
		UGeometryCollection* Asset;

		FGeometryCollectionContext(UGeometryCollection* InAsset, float InTime)
			: FContext(InTime)
			, Asset(InAsset)
		{}

	};
}

class FGeometryCollectionEditorToolkit : public FAssetEditorToolkit, public FNotifyHook, public FGCObject
{
public:
	void InitGeometryCollectionAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit);

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

	UDataflow* GetDataflow() { return Dataflow; }
	const UDataflow* GetDataflow() const { return Dataflow; }

	UGeometryCollection* GetGeometryCollection() { return GeometryCollection; }
	const UGeometryCollection* GetGeometryCollection() const { return GeometryCollection; }

	TSharedPtr<IDetailsView> GetPropertiesEditor() { return PropertiesEditor; }
	const TSharedPtr<IDetailsView> GetPropertiesEditor() const { return PropertiesEditor; }

	TSharedPtr<SGraphEditor> GetGraphEditor() { return GraphEditor; }
	const TSharedPtr<SGraphEditor> GetGraphEditor() const { return GraphEditor; }

private:
	static const FName GraphCanvasTabId;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedRef<SGraphEditor> CreateGraphEditorWidget(UDataflow* ObjectToEdit, TSharedPtr<IDetailsView> PropertiesEditor);

	static const FName PropertiesTabId;
	TSharedPtr<IDetailsView> PropertiesEditor;
	TSharedPtr<IDetailsView> CreatePropertiesEditorWidget(UObject* ObjectToEdit);

	UDataflow* Dataflow = nullptr;
	UGeometryCollection* GeometryCollection = nullptr;
};
