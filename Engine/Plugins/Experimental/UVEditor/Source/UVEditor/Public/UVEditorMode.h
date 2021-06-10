// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Tools/UEdMode.h"
#include "ToolTargets/ToolTarget.h"

#include "UVEditorMode.generated.h"

class FToolCommandChange;
class IUVUnwrapDynamicMesh;
class UMeshElementsVisualizer;
class UPreviewMesh;
class UToolTarget;
class UWorld;
class UInteractiveToolPropertySet;
class UUVToolStateObjectStore;
class UUVEditorToolMeshInput;

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

	void InitializeTargets(TArray<TObjectPtr<UObject>>& AssetsIn, TArray<FTransform>* TransformsIn);

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

	// We don't actually override MouseEnter, etc, because things get forwarded to the input
	// router via FEditorModeTools, and we don't have any additional input handling to do at the mode level.

protected:

	// UEdMode overrides
	virtual void CreateToolkit() override;
	virtual void ActivateDefaultTool() override;
	// Not sure whether we need these yet
	virtual void BindCommands() override;
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) {}
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) {}

	/**
	 * This stores the original objects that the UV editor was asked to operate on (for
	 * instance UStaticMesh pointers).
	 */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> OriginalObjectsToEdit;

	/**
	 * This stores tool targets created from OriginalObjectsToEdit that provide us with dynamic meshes.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UToolTarget>> ToolTargets;

	/**
	 * The actual input objects we give to the tools, including unwrapped meshes and previews, created
	 * out of ToolTargets.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> ToolInputObjects;


	/**
	 * Wireframes have to get ticked to be able to respond to setting changes.
	 */
	TArray<TWeakObjectPtr<UMeshElementsVisualizer>> WireframesToTick;

	/**
	 * If we want to have mode-level property objects, whether user-visible or not,
	 * they can be put here to be ticked.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveToolPropertySet>> PropertyObjectsToTick;

	// This keeps track of whether the meshes inside DisplayedMeshes have been changed since
	// the last time that they were baked back to their targets.
	TArray<int32> MeshChangeStamps;
};
