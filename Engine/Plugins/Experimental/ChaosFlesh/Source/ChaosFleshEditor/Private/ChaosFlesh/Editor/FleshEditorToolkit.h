// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "GraphEditor.h"
#include "ISkeletonTree.h"
#include "Misc/NotifyHook.h"
#include "TickableEditorObject.h"
#include "Toolkits/SimpleAssetEditor.h"

class FEditorViewportTabContent;
class IStructureDetailsView;
class ISkeletalView;
class FTabManager;
class IToolkitHost;
class UFleshAsset;
class UDataflow;
class USkeletalMesh;

namespace Dataflow
{
	class FFleshContext : public TEngineContext<FContextSingle>
	{
	public:
		DATAFLOW_CONTEXT_INTERNAL(TEngineContext<FContextSingle>, FFleshContext);

		FFleshContext(UObject* InOwner, UDataflow* InGraph, FTimestamp InTimestamp)
			: Super(InOwner, InGraph, InTimestamp)
		{}
	};
}

class FFleshEditorToolkit : public FAssetEditorToolkit, public FTickableEditorObject, public FNotifyHook, public FGCObject
{
public:
	void InitFleshAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit);

	//~ Begin DataflowEditorActions
	void OnPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent);
	bool OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage);
	void OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode);
	//~ End DataflowEditorActions


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
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GraphCanvas(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_NodeDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_AssetDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Skeletal(const FSpawnTabArgs& Args);

	UFleshAsset* GetFleshAsset() {return FleshAsset;}
	const UFleshAsset* GetFleshAsset() const { return FleshAsset; }

	UDataflow* GetDataflow() { return Dataflow; }
	const UDataflow* GetDataflow() const { return Dataflow; }

	TSharedPtr<Dataflow::FEngineContext> GetContext() { return Context; }
	TSharedPtr<const Dataflow::FEngineContext> GetContext() const { return Context; }

	//~ Begin FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableEditorObject Interface



	//~ Begin FAssetEditorToolkit interface
	virtual void GetSaveableObjects(TArray<UObject*>& OutObjects) const override;
	//virtual void SaveAsset_Execute() override;
	//virtual void SaveAssetAs_Execute() override;
	//virtual bool OnRequestClose() override;
	//~ End FAssetEditorToolkit interface


private:
	static const FName ViewportTabId;
	TSharedPtr<FEditorViewportTabContent> ViewportEditor;

	static const FName GraphCanvasTabId;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<FUICommandList> GraphEditorCommands;
	TSharedRef<SGraphEditor> CreateGraphEditorWidget(UDataflow* ObjectToEdit, TSharedPtr<IStructureDetailsView> InNodeDetailsEditor);

	static const FName NodeDetailsTabId;
	TSharedPtr<IStructureDetailsView> NodeDetailsEditor;
	TSharedPtr<IStructureDetailsView> CreateNodeDetailsEditorWidget(UObject* ObjectToEdit);

	static const FName AssetDetailsTabId;
	TSharedPtr<IDetailsView> AssetDetailsEditor;
	TSharedPtr<IDetailsView> CreateAssetDetailsEditorWidget(UObject* ObjectToEdit);


	static const FName SkeletalTabId;
	TObjectPtr<USkeleton> StubSkeleton;
	TObjectPtr<USkeletalMesh> StubSkeletalMesh;
	TSharedPtr<class ISkeletonTree> SkeletalEditor;
	TSharedPtr<ISkeletonTree> CreateSkeletalEditorWidget(USkeletalMesh* ObjectToEdit);

	UFleshAsset* FleshAsset = nullptr;
	UDataflow* Dataflow = nullptr;

	TSharedPtr<Dataflow::FEngineContext> Context;
	Dataflow::FTimestamp LastNodeTimestamp = Dataflow::FTimestamp::Invalid;

};