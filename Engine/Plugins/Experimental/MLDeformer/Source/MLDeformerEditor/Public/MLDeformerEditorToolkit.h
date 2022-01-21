// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerEditorData.h"

#include "IHasPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaViewport.h"
#include "PersonaAssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "MLDeformerPythonTrainingModel.h"
#include "Math/Color.h"

#include "Widgets/Notifications/SNotificationList.h"

class IDetailsView;
class FGGAssetEditorToolbar;
class UAnimInstance;
class UMLDeformerAsset;
class FMLDeformerEditorData;
class UMLDeformerComponent;
class UAnimPreviewInstance;
class UTextRenderComponent;
class UWorld;
class UMeshDeformer;

namespace MLDeformerEditorModes
{
	extern const FName Editor;
}

class FMLDeformerEditorToolkit :
	public FPersonaAssetEditorToolkit,
	public IHasPersonaToolkit,
	public FGCObject,
	public FEditorUndoClient,
	public FTickableEditorObject
{
public:
	friend class FMLDeformerApplicationMode;
	friend struct FMLDeformerVizSettingsTabSummoner;

	FMLDeformerEditorToolkit();
	virtual ~FMLDeformerEditorToolkit();

	/** Initialize the asset editor. This will register the application mode, init the preview scene, etc. */
	void InitAssetEditor(
		const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UMLDeformerAsset* DeformerAsset);

	/** FAssetEditorToolkit interface. */
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;

	/** FGCObject interface. */
	virtual FString GetReferencerName() const override { return TEXT("FMLDeformerEditorToolkit"); }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** FTickableEditorObject Interface. */
	virtual void Tick(float DeltaTime) override {};
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override;

	/** IHasPersonaToolkit interface. */
	virtual TSharedRef<IPersonaToolkit> GetPersonaToolkit() const override;

	FMLDeformerEditorData* GetEditorData() const { return EditorData.Get(); }

private:
	/* Toolbar related. */
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	/** Preview scene setup. */
	void HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
	void HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport);
	void HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView);
	void OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Helpers. */
	void CreateSkinnedActor(EMLDeformerEditorActorIndex ActorIndex, const FName& Name, UWorld* World, USkeletalMesh* Mesh, const FLinearColor LabelColor, FLinearColor WireframeColor) const;
	void CreateBaseActor(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene, const FName& Name, FLinearColor LabelColor, FLinearColor WireframeColor);
	void CreateGeomCacheActor(EMLDeformerEditorActorIndex ActorIndex, UWorld* World, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor);
	UMLDeformerComponent* AddMLDeformerComponentToActor(EMLDeformerEditorActorIndex ActorIndex);
	void AddMeshDeformerToActor(EMLDeformerEditorActorIndex ActorIndex, UMeshDeformer* MeshDeformer) const;
	bool TryLoadOnnxFile() const;
	void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State, bool PlaySound) const;
	UTextRenderComponent* CreateLabelForActor(AActor* Actor, UWorld* World, FLinearColor Color, const FText& Text) const;
	FText GetOverlayText() const;
	void SetComputeGraphDataProviders() const;
	void OnSwitchedVisualizationMode();
	bool HandleTrainingResult(ETrainingResult TrainingResult, double TrainingDuration);
	void UpdateActorVisibility();

private:
	/** The editor data, containing things like the skeletal mesh, anim instance, etc. */
	TSharedPtr<FMLDeformerEditorData> EditorData;
};
