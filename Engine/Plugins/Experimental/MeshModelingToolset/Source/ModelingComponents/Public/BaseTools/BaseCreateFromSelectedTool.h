// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseGizmos/TransformGizmo.h"
#include "MeshOpPreviewHelpers.h"
#include "PropertySets/OnAcceptProperties.h"
#include "BaseCreateFromSelectedTool.generated.h"


/**
 * ToolBuilder for UBaseCreateFromSelectedTool
 */
UCLASS()
class MODELINGCOMPONENTS_API UBaseCreateFromSelectedToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;
	
	virtual TOptional<int32> MaxComponentsSupported() const { return TOptional<int32>(); }
	virtual int32 MinComponentsSupported() const { return 1; }
	
	// subclass must override!
	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const { check(false); return nullptr; }

};


UENUM()
enum class EBaseCreateFromSelectedTargetType
{
	/** Create a new asset containing the result mesh */
	NewAsset,
	/** Store the result mesh in the first selected input asset */
	FirstInputAsset,
	/** Store the result mesh in the last selected input asset */
	LastInputAsset
};


UCLASS()
class MODELINGCOMPONENTS_API UBaseCreateFromSelectedHandleSourceProperties : public UOnAcceptHandleSourcesProperties
{
	GENERATED_BODY()
public:
	/** Where should the output mesh produced by this operation be stored */
	UPROPERTY(EditAnywhere, Category = ToolOutputOptions)
	EBaseCreateFromSelectedTargetType WriteOutputTo = EBaseCreateFromSelectedTargetType::NewAsset;

	/** Base name for newly-generated asset */
	UPROPERTY(EditAnywhere, Category = ToolOutputOptions, meta = (TransientToolProperty, EditCondition = "WriteOutputTo == EBaseCreateFromSelectedTargetType::NewAsset", EditConditionHides))
	FString OutputName;

	/** Name of asset that will be updated */
	UPROPERTY(VisibleAnywhere, Category = ToolOutputOptions, meta = (TransientToolProperty, EditCondition = "WriteOutputTo != EBaseCreateFromSelectedTargetType::NewAsset", EditConditionHides))
	FString OutputAsset;
};





/**
 * Properties of UI to adjust input meshes
 */
UCLASS()
class MODELINGCOMPONENTS_API UTransformInputsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Show UI to allow changing translation, rotation and scale of input meshes */
	UPROPERTY(EditAnywhere, Category = Transform)
	bool bShowTransformUI = true;

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Transform, meta = (EditCondition = "bShowTransformUI == true"))
	bool bSnapToWorldGrid = false;
};


/**
 * UBaseCreateFromSelectedTool is a base Tool (must be subclassed) 
 * that provides support for common functionality in tools that create a new mesh from a selection of one or more existing meshes
 */
UCLASS()
class MODELINGCOMPONENTS_API UBaseCreateFromSelectedTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UBaseCreateFromSelectedTool() = default;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	//
	// InteractiveTool API - generally does not need to be modified by subclasses
	//

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	

protected:

	//
	// UBaseCreateFromSelectedTool API - subclasses typically implement these functions
	//

	/**
	 * After preview is created, this is called to convert inputs and set preview materials
	 * (grouped together because materials may come from inputs)
	 * Subclasses should always implement this.
	 * @param bSetPreviewMesh If true, function may try to set an initial "early" preview mesh to have some initial surface on tool start.  (Not all tools will actually create this.)
	 *						  This boolean is here in case a subclass needs to call this setup function again later (e.g. to change the materials used), when it won't need or want the preview surface to be created
	 */
	virtual void ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh = true) { check(false);  }

	/** overload to initialize any added properties in subclasses; called during setup */
	virtual void SetupProperties() {}

	/** overload to save any added properties in the subclasses; called on shutdown */
	virtual void SaveProperties() {}

	/** optional overload to set callbacks on preview, e.g. to visualize results; called after preview is created. */
	virtual void SetPreviewCallbacks() {}

	/** Return the name to be used for generated assets.  Note: Asset name will be prefixed by source actor name if only actor was selected. */
	virtual FString GetCreatedAssetName() const { return TEXT("Generated"); }

	/** Return the name of the action to be used in the Undo stack */
	virtual FText GetActionName() const;

	/** Return the materials to be used on the output mesh on tool accept; defaults to the materials set on the preview */
	virtual TArray<UMaterialInterface*> GetOutputMaterials() const;



	/**
	 * IDynamicMeshOperatorFactory implementation that subclass must override and implement
	 */
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override
	{
		check(false);
		return TUniquePtr<FDynamicMeshOperator>();
	}

protected:

	/** Helper to build asset names */
	FString PrefixWithSourceNameIfSingleSelection(const FString& AssetName) const;

	// Helpers for managing transform gizoms; typically do not need to be overloaded
	virtual void UpdateGizmoVisibility();
	virtual void SetTransformGizmos();
	virtual void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	// Helper to generate assets when a result is accepted; typically does not need to be overloaded
	virtual void GenerateAsset(const FDynamicMeshOpResult& Result);

	// Helper to generate assets when a result is accepted; typically does not need to be overloaded
	virtual void UpdateAsset(const FDynamicMeshOpResult& Result, TUniquePtr<FPrimitiveComponentTarget>& Target);

protected:

	UPROPERTY()
	UTransformInputsToolProperties* TransformProperties;

	UPROPERTY()
	UBaseCreateFromSelectedHandleSourceProperties* HandleSourcesProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UPROPERTY()
	TArray<UTransformProxy*> TransformProxies;

	UPROPERTY()
	TArray<UTransformGizmo*> TransformGizmos;

	UPROPERTY()
	TArray<FVector> TransformInitialScales;


	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;

};

