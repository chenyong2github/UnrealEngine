// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CanvasTypes.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "EditorViewportClient.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "SAssetEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FDataflowEditorToolkit;
class FAdvancedPreviewScene;
class FEditorViewportClient;
class FDataflowEditorViewportClient;
class SEditorViewport;
class ADataflowActor;
class ADataflowRenderingActor;
class FDynamicMeshBuilder;

// ----------------------------------------------------------------------------------

class SDataflowEditorViewport : public SAssetEditorViewport, public ICommonEditorViewportToolbarInfoProvider, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDataflowEditorViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDataflowEditorToolkit>, DataflowEditorToolkit)
	SLATE_END_ARGS()

	SDataflowEditorViewport();

	void Construct(const FArguments& InArgs);

	//~ ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;

	//~ FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override{return TEXT("SDataflowEditorViewport");}


protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
private:
	/// The scene for this viewport. 
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;

	/// Editor viewport client 
	TSharedPtr<FDataflowEditorViewportClient> EditorViewportClient;

	TWeakPtr<FDataflowEditorToolkit> DataflowEditorToolkitPtr;
	ADataflowActor* CustomDataflowActor = nullptr;
	ADataflowRenderingActor* CustomDataflowRenderingActor = nullptr;
};


// ----------------------------------------------------------------------------------

class FDataflowEditorViewportClient : public FEditorViewportClient
{
public:
	using Super = FEditorViewportClient;
	
	FDataflowEditorViewportClient(FPreviewScene* InPreviewScene,
		const TWeakPtr<SEditorViewport> InEditorViewportWidget = nullptr,
		TWeakPtr<FDataflowEditorToolkit> InDataflowEditorToolkitPtr = nullptr);

	Dataflow::FTimestamp LatestTimestamp(const UDataflow* Dataflow, const Dataflow::FContext* Context);
	void SetDataflowRenderingActor(ADataflowRenderingActor* InActor) { DataflowRenderingActor = InActor; }

	// FEditorViewportClient interface
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void Tick(float DeltaSeconds) override;
	// End of FEditorViewportClient

	//~ FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDataflowEditorViewportClient"); }

private:
	void RenderIntoStructures();
	void ReleaseRenderStructures();

	TWeakPtr<FDataflowEditorToolkit> DataflowEditorToolkitPtr = nullptr;
	Dataflow::FTimestamp LastModifiedTimestamp = Dataflow::FTimestamp::Invalid;
	ADataflowRenderingActor* DataflowRenderingActor = nullptr;

	FManagedArrayCollection RenderCollection;

	// Renderables
	bool bRenderMesh = false;
	TArray<uint32> IndexBuffer;
	TArray<FDynamicMeshVertex> VertexBuffer;
	TUniquePtr<FDynamicMeshBuilder> MeshBuilder;

};
