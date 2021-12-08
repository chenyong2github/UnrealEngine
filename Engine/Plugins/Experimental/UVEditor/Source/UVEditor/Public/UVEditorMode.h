// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Tools/UEdMode.h"
#include "ToolTargets/ToolTarget.h"
#include "GeometryBase.h"

#include "UVEditorMode.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);

class FEditorViewportClient;
class FAssetEditorModeManager;
class FToolCommandChange;
class IUVUnwrapDynamicMesh;
class UMeshElementsVisualizer;
class UPreviewMesh;
class UToolTarget;
class UWorld;
class FUVEditorModeToolkit;
class UInteractiveToolPropertySet; 
class UMeshOpPreviewWithBackgroundCompute;
class UUVToolStateObjectStore;
class UUVEditorToolMeshInput;
class UUVEditorBackgroundPreview;
class UUVToolViewportButtonsAPI;


/**
 * The UV editor mode is the mode used in the UV asset editor. It holds most of the inter-tool state.
 * We put things in a mode instead of directly into the asset editor in case we want to someday use the mode
 * in multiple asset editors.
 */
UCLASS(Transient)
class UUVEditorMode : public UEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_UVEditorModeId;

	UUVEditorMode();

	void RegisterTools();

	/**
	 * Gets the tool target requirements for the mode. The resulting targets undergo further processing
	 * to turn them into the input objects that tools get (since these need preview meshes, etc).
	 */
	static const FToolTargetTypeRequirements& GetToolTargetRequirements();

	/**
	 * Gets the factor by which UV layer unwraps get scaled (scaling makes certain things easier, like zooming in, etc).
	 */
	static double GetUVMeshScalingFactor() { return 1000; }

	// Both initialization functions must be called for things to function properly. InitializeContexts should
	// be done first so that the 3d preview world is ready for creating meshes in InitializeTargets.
	void InitializeContexts(FEditorViewportClient& LivePreviewViewportClient, FAssetEditorModeManager& LivePreviewModeManager,
		UUVToolViewportButtonsAPI& ViewportButtonsAPI);
	void InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn, const TArray<FTransform>& TransformsIn);

	// Public for use by undo/redo. Otherwise should use RequestUVChannelChange
	void ChangeInputObjectLayer(int32 AssetID, int32 NewLayerIndex);

	/** 
	 * Request a change of the displayed UV channel/layer. It will happen on the next tick, and 
	 * create an undo/redo event.
	 */
	void RequestUVChannelChange(int32 AssetID, int32 Channel);

	bool IsActive() { return bIsActive; }

	// TODO: We'll probably eventually want a function like this so we can figure out how big our 3d preview
	// scene is and how we should position the camera intially...
	//FBoxSphereBounds GetLivePreviewBoundingBox() const;

	// Unlike UInteractiveToolManager::EmitObjectChange, emitting an object change using this
	// function does not cause it to expire when the active tool doesn't match the emitting tool.
	// It is important that the emitted change deals properly with expiration itself, for instance
	// expiring itself when a tool input is invalid or a contained preview is disconnected.
	// TODO: Should this sort of option exist in UInteractiveToolManager?
	void EmitToolIndependentObjectChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description);

	// Asset management
	bool HaveUnappliedChanges();
	void GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut);
	void ApplyChanges();

	/** @return List of asset names, indexed by AssetID */
	const TArray<FString>& GetAssetNames() const { return AssetNames; }

	/** @return Number of UV channels in the given asset, or IndexConstants::InvalidID if AssetID was invalid.  */
	int32 GetNumUVChannels(int32 AssetID) const;

	/** @return The index of the channel currently displayed for the given AssetID. */
	int32 GetDisplayedChannel(int32 AssetID) const;

	/** @return A settings object suitable for display in a details panel to control the background visualization. */
	UObject* GetBackgroundSettingsObject();

	// UEdMode overrides
	virtual void Enter() override;
	virtual bool ShouldToolStartBeAllowed(const FString& ToolIdentifier) const override;
	virtual void Exit() override;
	virtual void ModeTick(float DeltaTime) override;
	// We're changing visibility of this one to public here so that we can call it from the toolkit
	// when clicking accept/cancel buttons. We don't want to friend the toolkit because we don't want
	// it to later get (accidentally) more entangled with mode internals. At the same time, we're not
	// sure whether we want to make the UEdMode one public. So this is the minimal-impact tweak.
	virtual void ActivateDefaultTool() override;

	// This is currently not part of the base class at all... Should it be?
	virtual bool IsDefaultToolActive();

	// We don't actually override MouseEnter, etc, because things get forwarded to the input
	// router via FEditorModeTools, and we don't have any additional input handling to do at the mode level.

	// Holds the background visualiztion
	UPROPERTY()
	TObjectPtr<UUVEditorBackgroundPreview> BackgroundVisualization;

protected:

	// UEdMode overrides
	virtual void CreateToolkit() override;
	// Not sure whether we need these yet
	virtual void BindCommands() override;
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override {}
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override {}
	
	void UpdateTriangleMaterialBasedOnBackground(bool IsBackgroundVisible);
	void UpdatePreviewMaterialBasedOnBackground();

	/**
	 * Stores original input objects, for instance UStaticMesh pointers. AssetIDs on tool input 
	 * objects are indices into this array (and ones that are 1:1 with it)
	 */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> OriginalObjectsToEdit;

	/**
	 * Tool targets created from OriginalObjectsToEdit (and 1:1 with that array) that provide
	 * us with dynamic meshes whose UV layers we unwrap.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UToolTarget>> ToolTargets;

	/**
	 * Transforms that should be used for the 3d previews, 1:1 with OriginalObjectsToEdit
	 * and ToolTargets.
	 */
	TArray<FTransform> Transforms;

	/**
	 * 1:1 with the asset arrays. Hold dynamic mesh representations of the targets. These
	 * are authoritative versions of the combined UV layers that get baked back on apply.
	 */
	TArray<TSharedPtr<UE::Geometry::FDynamicMesh3>> AppliedCanonicalMeshes;

	/**
	 * 1:1 with AppliedCanonicalMeshes, the actual displayed 3d meshes that can be used
	 * by tools for background computations. However if doing so, keep in mind that while
	 * we currently do not display more than one layer at a time for each asset, if we
	 * someday do, tools would have to take care to disallow cases where the two layers
	 * of the same asset might try to use the same preview for a background compute.
	 */
	TArray<TObjectPtr<UMeshOpPreviewWithBackgroundCompute>> AppliedPreviews;

	/**
	 * Input objects we give to the tools, one per displayed UV layer. This includes pointers
	 * to the applied meshes, but also contains the unwrapped mesh and preview. These should
	 * not be assumed to be the same length as the asset arrays in case we someday do not
	 * display exactly a single layer per asset.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> ToolInputObjects;

	/**
	 * Wireframes have to get ticked to be able to respond to setting changes.
	 * This is 1:1 with ToolInputObjects.
	 */
	TArray<TWeakObjectPtr<UMeshElementsVisualizer>> WireframesToTick;


	// Authoritative list of targets that have changes that have not been baked back yet.
	TSet<int32> ModifiedAssetIDs;

	// 1:1 with ToolTargets, indexed by AssetID
	TArray<FString> AssetNames;

	// Used with the ToolAssetAndLayerAPI to process tool layer change requests
	void SetDisplayedUVChannels(const TArray<int32>& LayerPerAsset, bool bEmitUndoTransaction);

	TArray<int32> PendingUVLayerIndex;

	// Here largely for convenience to avoid having to pass it around functions.
	UPROPERTY()
	TObjectPtr<UWorld> LivePreviewWorld = nullptr;

	/**
	 * Mode-level property objects (visible or not) that get ticked.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveToolPropertySet>> PropertyObjectsToTick;

	bool bIsActive = false;
	FString DefaultToolIdentifier;

	static FDateTime AnalyticsLastStartTimestamp;
};

