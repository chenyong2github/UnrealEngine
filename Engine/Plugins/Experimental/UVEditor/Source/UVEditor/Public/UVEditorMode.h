// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Tools/UEdMode.h"
#include "ToolTargets/ToolTarget.h"
#include "GeometryBase.h"

#include "UVEditorMode.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);

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
class UUVEditorUVChannelProperties;


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

	void InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn, const TArray<FTransform>& TransformsIn);

	// public for use by undo/redo
	void ChangeInputObjectLayer(int32 AssetID, int32 NewLayerIndex, bool bForceRebuild=false);
	void UpdateSelectedLayer();

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

	// UEdMode overrides
	virtual void Enter() override;
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
	void AddDisplayedPropertySet(const TObjectPtr<UInteractiveToolPropertySet>& PropertySet);

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

	// Helper array of canonical unwrap changestamps that allows us to detect changes to UVs.
	// Used to populate ModifiedAssetIDs on tick.
	TArray<int32> LastSeenChangeStamps;


	/** Used as a selector of UV channels/layers of opened assets in the editor. */
	UPROPERTY()
	TObjectPtr<UUVEditorUVChannelProperties> UVChannelProperties = nullptr;

	// Used with UVChannelProperties
	void SwitchActiveAsset(const FString& UVAsset);
	void SwitchActiveChannel(const FString& UVChannel);

	// Used with the ToolAssetAndLayerAPI to process tool layer change requests
	void ForceUpdateDisplayChannel(const TArray<int32>& LayerPerAsset, bool bForceRebuildUnwrap, bool bEmitUndoTransaction);

	// Used to change layers with our current picker approach (we need to remember the previous
	// layer value so we know which one to remove). Likely to change if we start adding/removing
	// using some different UI.
	TArray<int32> PreviousUVLayerIndex;
	TArray<int32> PendingUVLayerIndex;
	bool  bPendingUVLayerChangeDestroyTool = true;
	bool  bForceRebuildUVLayer = false;


	// Wireframe Display Properties
	float TriangleOpacity = 1.0;
	float TriangleDepthOffset = 0.5;
	const float WireframeDepthOffset = 0.6;
	FColor TriangleColor = FColor(50, 194, 219);
	FColor WireframeColor = FColor(50, 100, 219);
	FColor IslandBorderColor = FColor(103, 52, 235);

	// Here largely for convenience to avoid having to pass it around functions.
	UPROPERTY()
	TObjectPtr<UWorld> LivePreviewWorld = nullptr;

	/**
	* Mode-level property objects to display in the details panel.
	*/
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveToolPropertySet>> PropertyObjectsToDisplay;

	/**
	 * Mode-level property objects (visible or not) that get ticked.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveToolPropertySet>> PropertyObjectsToTick;

	bool bIsActive = false;

	static FDateTime AnalyticsLastStartTimestamp;
};

