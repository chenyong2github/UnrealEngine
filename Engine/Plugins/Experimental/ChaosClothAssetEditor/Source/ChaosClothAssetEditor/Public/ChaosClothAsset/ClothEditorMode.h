// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditorMode.h"
#include "GeometryBase.h"
#include "Delegates/IDelegateInstance.h"
#include "ClothEditorMode.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);

class FEditorViewportClient;
class FAssetEditorModeManager;
class FToolCommandChange;
class UMeshElementsVisualizer;
class UPreviewMesh;
class UToolTarget;
class FToolTargetTypeRequirements;
class UWorld;
class FChaosClothAssetEditorModeToolkit;
class UInteractiveToolPropertySet; 
class UMeshOpPreviewWithBackgroundCompute;
class UClothToolViewportButtonsAPI;
class UDynamicMeshComponent;
class UChaosClothComponent;
class FEditorViewportClient;
class FChaosClothEditorRestSpaceViewportClient;

/**
 * The cloth editor mode is the mode used in the cloth asset editor. It holds most of the inter-tool state.
 * We put things in a mode instead of directly into the asset editor in case we want to someday use the mode
 * in multiple asset editors.
 */
UCLASS(Transient)
class CHAOSCLOTHASSETEDITOR_API UChaosClothAssetEditorMode : public UBaseCharacterFXEditorMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_ChaosClothAssetEditorModeId;

	UChaosClothAssetEditorMode();

	virtual void Enter() override;

	/**
	 * Gets the tool target requirements for the mode. The resulting targets undergo further processing
	 * to turn them into the input objects that tools get (since these need preview meshes, etc).
	 */
	static const FToolTargetTypeRequirements& GetToolTargetRequirements();

	// Both initialization functions must be called for things to function properly. InitializeContexts should
	// be done first so that the 3d preview world is ready for creating meshes in InitializeTargets.
	void InitializeContexts(FEditorViewportClient& LivePreviewViewportClient, FAssetEditorModeManager& LivePreviewModeManager);

	virtual void InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn);

	// Asset management
	bool HaveUnappliedChanges();
	void GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut);
	void ApplyChanges();

	/** @return List of asset names, indexed by AssetID */
	const TArray<FString>& GetAssetNames() const { return AssetNames; }

	// UEdMode overrides
	virtual bool ShouldToolStartBeAllowed(const FString& ToolIdentifier) const override;
	virtual void Exit() override;
	virtual void ModeTick(float DeltaTime) override;
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void PostUndo() override;

	// We don't actually override MouseEnter, etc, because things get forwarded to the input
	// router via FEditorModeTools, and we don't have any additional input handling to do at the mode level.

	// Bounding box for rest-space meshes
	virtual FBox SceneBoundingBox() const override;
	FBox SelectionBoundingBox() const;		// only selected mesh components

	// Bounding box for sim-space meshes
	FBox PreviewBoundingBox() const;

	// Toggle between 2D pattern and 3D rest space mesh view
	bool IsPattern2DModeActive() const;
	void TogglePatternMode();
	bool CanTogglePatternMode() const;

private:

	friend class FChaosClothAssetEditorToolkit;
	void SetRestSpaceViewportClient(TWeakPtr<FChaosClothEditorRestSpaceViewportClient, ESPMode::ThreadSafe> ViewportClient);
	void RefocusRestSpaceViewportClient();

	// UBaseCharacterFXEditorMode
	virtual void AddToolTargetFactories() override;
	virtual void RegisterTools() override;
	virtual void CreateToolTargets(const TArray<TObjectPtr<UObject>>& AssetsIn);

	// UEdMode overrides
	virtual void CreateToolkit() override;
	// Not sure whether we need these yet
	virtual void BindCommands() override;
	
	// Transforms that should be used for the 3D previews, 1:1 with OriginalObjectsToEdit
	// and ToolTargets.
	TArray<FTransform> Transforms;

	// Preview simulation mesh
	UPROPERTY()
	TObjectPtr<UChaosClothComponent> ClothComponent;

	// Rest-space wireframes. They have to get ticked to be able to respond to setting changes. 
	UPROPERTY()
	TArray<TObjectPtr<UMeshElementsVisualizer>> WireframesToTick;

	// Authoritative list of targets that have changes that have not been baked back yet.
	TSet<int32> ModifiedAssetIDs;

	// 1:1 with ToolTargets, indexed by AssetID
	TArray<FString> AssetNames;

	// Here largely for convenience to avoid having to pass it around functions.
	UPROPERTY()
	TObjectPtr<UWorld> PreviewWorld = nullptr;
	
	// Mode-level property objects (visible or not) that get ticked.
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveToolPropertySet>> PropertyObjectsToTick;

	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> DynamicMeshComponents;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> DynamicMeshComponentParentActors;

	struct FDynamicMeshSourceInfo
	{
		int32 LodIndex;
		int32 PatternIndex;
	};

	TArray<FDynamicMeshSourceInfo> DynamicMeshSourceInfos;

	TWeakPtr<FChaosClothEditorRestSpaceViewportClient, ESPMode::ThreadSafe> RestSpaceViewportClient;

	FDelegateHandle SelectionModifiedEventHandle;

	// Whether to display the 2D pattern or 3D rest configuration in the left viewport
	bool bPattern2DMode = false;

	// If we can switch between 2D and 3D rest configuration
	bool bCanTogglePattern2DMode = true;

	// Whether to combine all patterns into a single DynamicMeshComponent, or have separate components for each pattern
	// TODO: Expose this to the user
	bool bCombineAllPatterns = false;

	// Create dynamic mesh components from the cloth component's rest space info
	void ReinitializeDynamicMeshComponents();

	// Extract the rest space mesh from the given tool target
	void GetRestSpaceMesh(UToolTarget* ToolTarget, UE::Geometry::FDynamicMesh3& RestSpaceMesh);

	// Set up the preview simulation mesh from the given rest-space mesh
	void UpdateSimulationMeshes();

};

