// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "EditorViewportClient.h"
#include "SAssetEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FFleshEditorToolkit;
class FAdvancedPreviewScene;
class FEditorViewportClient;
class FFleshEditorViewportClient;
class SEditorViewport;
class AFleshActor;
class ADataflowRenderingActor;

// ----------------------------------------------------------------------------------

class SFleshEditorViewport : public SAssetEditorViewport, public ICommonEditorViewportToolbarInfoProvider, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SFleshEditorViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FFleshEditorToolkit>, FleshEditorToolkit)
	SLATE_END_ARGS()

	SFleshEditorViewport();

	void Construct(const FArguments& InArgs);

	//~ ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;

	//~ FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override{return TEXT("SFleshEditorViewport");}

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
private:
	/// The scene for this viewport. 
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;

	/// Editor viewport client 
	TSharedPtr<FFleshEditorViewportClient> EditorViewportClient;

	TWeakPtr<FFleshEditorToolkit> FleshEditorToolkitPtr;

	AFleshActor* CustomFleshActor = nullptr;
	ADataflowRenderingActor* CustomDataflowRenderingActor = nullptr;
};


// ----------------------------------------------------------------------------------

class FFleshEditorViewportClient : public FEditorViewportClient
{
public:
	using Super = FEditorViewportClient;
	
	FFleshEditorViewportClient(FPreviewScene* InPreviewScene,
		const TWeakPtr<SEditorViewport> InEditorViewportWidget = nullptr,
		TWeakPtr<FFleshEditorToolkit> InFleshEditorToolkitPtr = nullptr);


	void SetDataflowRenderingActor(ADataflowRenderingActor* InActor) { DataflowRenderingActor = InActor; }
	Dataflow::FTimestamp LatestTimestamp(const UDataflow* Dataflow, const Dataflow::FContext* Context);

	// FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	// End of FEditorViewportClient

	//~ FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FFleshEditorViewportClient"); }

	TWeakPtr<FFleshEditorToolkit> FleshEditorToolkitPtr = nullptr;
	ADataflowRenderingActor* DataflowRenderingActor = nullptr;
	Dataflow::FTimestamp LastModifiedTimestamp = Dataflow::FTimestamp::Invalid;
};
