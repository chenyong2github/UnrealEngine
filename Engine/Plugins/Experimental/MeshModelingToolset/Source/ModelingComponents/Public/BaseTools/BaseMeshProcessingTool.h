// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h"
#include "BaseMeshProcessingTool.generated.h"

// predeclarations
struct FMeshDescription;
class FMeshNormals;
class USimpleDynamicMeshComponent;
class UPreviewMesh;

/**
 * ToolBuilder for UBaseMeshProcessingTool
 */
UCLASS()
class MODELINGCOMPONENTS_API UBaseMeshProcessingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

public:
	
	virtual bool SupportsMultipleObjects() const { return false;  }
	
	// subclass must override!
	virtual UBaseMeshProcessingTool* MakeNewToolInstance(UObject* Outer) const { check(false); return nullptr; }

};




/**
 * UBaseMeshProcessingTool is a base Tool (ie has no functionality of it's own and must be subclassed) 
 * that provides the following structure:
 *   - a Background-Compute-With-Preview Temp Actor/Component is created based on the input mesh
 *   - The Subclass provides FDynamicMeshOperator instances (via IDynamicMeshOperatorFactory) that process/modify and update this Preview
 *   - PropertySets with custom visibility can be registered, and on change will invalidate the current computation
 *   
 * Most subclasses will only need to define their PropertySets and implement MakeNewOperator(), see eg SmoothMeshTool for a minimal example
 *
 * Other functions:
 *   - GetInitialMesh() : return reference to copy of initial mesh, used to initialize FDynamicMeshOperator
 *   - GetUPreviewMesh() : return the UPreviewMesh inside the background compute (for configuration/etc - should not directly touch the mesh!)
 *   - GetPreviewTransform() : return active FTransform on the Preview mesh, should be passed to FDynamicMeshOperator unless it is outputting world position
 *   - InvalidateResult() : subclasses call this to notify the base class that current result/computation has been invalidated
 *
 * The Base tool will do various optional precomputations or changes to the input mesh, which can be configured by
 * overriding various functions below.
 * 
 *   RequiresBaseNormals() : return true (default) to calculate per-vertex normals on the input mesh, returned by GetBaseNormals()
 * 
 *   RequiresScaleNormalization() : return true (default) to apply an initial scale to the input mesh so that it has consistent size
 *     before being sent into the computation. Scaling factor (eg for scaling UI constants) can be accessed via GetScaleNormalizationFactor()
 *
 */
UCLASS()
class MODELINGCOMPONENTS_API UBaseMeshProcessingTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UBaseMeshProcessingTool() = default;

	virtual void SetWorld(UWorld* World);

	//
	// InteractiveTool API - generally does not need to be modified by subclasses
	//

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;


protected:

	//
	// UBaseMeshProcessingTool REQUIRED API - subclasses must implement these functions
	//

	/**
	 * IDynamicMeshOperatorFactory implementation that subclass must override and implement
	 */
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override
	{
		check(false);
		return TUniquePtr<FDynamicMeshOperator>();
	}


	/**
	 * called when Tool is Accepted to determine whether it is safe to only update vertex positions, or if entire target mesh must be replaced.
	 */
	virtual bool HasMeshTopologyChanged() const
	{
		check(false);
		return true;
	}

	//
	// UBaseMeshProcessingTool OPTIONAL API - subclasses may implement these functions
	//

	/** @return text string shown to user for Accept Transaction that updates input mesh. Subclass should override. */
	virtual FText GetToolMessageString() const;

	/** @return text string shown to user for Accept Transaction that updates input mesh. Subclass should override. */
	virtual FText GetAcceptTransactionName() const;

	/** This function is called during ::Setup() to allow subclass to register property sets, before kicking off initial computation */
	virtual void InitializeProperties() {}

	/** This function is called during ::Shutdown so that subclass may perform final processing and save property sets */
	virtual void OnShutdown(EToolShutdownType ShutdownType) {}


	//
	// Optional Property Set API - subclasses can use this to manage property sets with configurable visibility that invalidate the precompute
	//

protected:

	/**
	 * Register an optional property set with the given VisibilityFunc
	 */
	template<class PropSetType>
	PropSetType* AddOptionalPropertySet(TUniqueFunction<bool()> VisibilityFunc, bool bChangeInvalidatesResult = true)
	{
		return AddOptionalPropertySet<PropSetType>(MoveTemp(VisibilityFunc), []() {}, bChangeInvalidatesResult);
	}

	/**
	 * Register an optional property set with the given VisibilityFunc, and call OnModifiedFunc if any of the properties change
	 */
	template<class PropSetType>
	PropSetType* AddOptionalPropertySet( TUniqueFunction<bool()> VisibilityFunc, TUniqueFunction<void()> OnModifiedFunc, bool bChangeInvalidatesResult)
	{
		PropSetType* PropSet = NewObject<PropSetType>(this);
		AddOptionalPropertySet(PropSet, MoveTemp(VisibilityFunc), MoveTemp(OnModifiedFunc), bChangeInvalidatesResult);
		return PropSet;
	}

	/** Call this function to update optional property sets visibility. Should call base implementation */
	virtual void UpdateOptionalPropertyVisibility();

protected:

	virtual void AddOptionalPropertySet(UInteractiveToolPropertySet* PropSet, 
		TUniqueFunction<bool()> VisibilityFunc, 
		TUniqueFunction<void()> OnModifiedFunc,
		bool bChangeInvalidatesResult);

	struct FOptionalPropertySet
	{
		TUniqueFunction<bool()> IsVisible;
		TUniqueFunction<void()> OnModifiedFunc;
		bool bInvalidateOnModify;
		TWeakObjectPtr<UInteractiveToolPropertySet> PropertySet;
	};

	TArray<FOptionalPropertySet> OptionalProperties;
	virtual void OnOptionalPropSetModified(int32 Index);
	virtual void SavePropertySets();


protected:
	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;

	// Preview object holds temporary Actor with preview mesh component
	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	UPreviewMesh* GetUPreviewMesh() const { return Preview->PreviewMesh; }
	const FTransform& GetPreviewTransform() const { return OverrideTransform; }


private:
	bool bResultValid = false;
protected:
	virtual void InvalidateResult();
	virtual void UpdateResult();


	//
	// Initial data, used to initialize background compute / etc
	//

private:
	FDynamicMesh3 InitialMesh;
protected:
	/** @return duplciate of initial mesh (possibly with optional size normalization) */
	const FDynamicMesh3& GetInitialMesh() const { return InitialMesh;  }
	/** @return duplciate of initial mesh (possibly with optional size normalization) */
	FDynamicMesh3& GetInitialMesh() { return InitialMesh; }



	//
	// Optional base mesh per-vertex normals
	//
protected:
	/** 
	 * If this function returns true, BaseNormals will be initialized in Tool ::Setup(). 
	 * This has some cost and should be disabled if not necessary. 
	 */
	virtual bool RequiresBaseNormals() const { return true; }

	/** @return calculated base normals. This pointer does not change for the lifetime of the Tool. */
	TSharedPtr<FMeshNormals>& GetBaseNormals();

private:
	TSharedPtr<FMeshNormals> BaseNormals;


	//
	// Optional uniform scale applied to mesh. Enabled by default
	// 

protected:
	/** 
	 * If this function returns true, input mesh will be scaled to normalized dimension in ::Setup() before any processing begins.
	 * This scaling will be undone on Accept()
	 */
	virtual bool RequiresScaleNormalization() const { return true; }

	double GetScaleNormalizationFactor() const { return 1.0 / SrcScale; };

private:
	// scale/translate applied to input mesh to regularize it
	bool bIsScaleNormalizationApplied = false;
	FVector3d SrcTranslate;
	double SrcScale;
	// transform that does the opposite of scale/translate so that mesh stays in the right spot on screen
	FTransform OverrideTransform;
};
