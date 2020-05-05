// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "CleaningOps/EditNormalsOp.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "EditNormalsTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditNormalsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditNormalsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UEditNormalsToolProperties();

	bool WillTopologyChange()
	{
		return bFixInconsistentNormals || bInvertNormals || SplitNormalMethod != ESplitNormalMethod::UseExistingTopology;
	}

	/** Recompute all mesh normals */
	UPROPERTY(EditAnywhere, Category = NormalsCalculation, meta = (EditCondition = "SplitNormalMethod == ESplitNormalMethod::UseExistingTopology"))
	bool bRecomputeNormals;

	/** Choose the method for computing vertex normals */
	UPROPERTY(EditAnywhere, Category = NormalsCalculation)
	ENormalCalculationMethod NormalCalculationMethod;

	/** For meshes with inconsistent triangle orientations/normals, flip as needed to make the normals consistent */
	UPROPERTY(EditAnywhere, Category = NormalsCalculation)
	bool bFixInconsistentNormals;

	/** Invert (flip) all mesh normals and associated triangle orientations */
	UPROPERTY(EditAnywhere, Category = NormalsCalculation)
	bool bInvertNormals;

	/** Control whether and how the topology of the normals is recomputed, e.g. to create sharp edges where face normals change by a large amount or where face group IDs change.  Normals will always be recomputed unless SplitNormal Method is UseExistingTopology. */
	UPROPERTY(EditAnywhere, Category = NormalsTopology)
	ESplitNormalMethod SplitNormalMethod;

	/** Threshold on angle of change in face normals across an edge, above which we create a sharp edge if bSplitNormals is true */
	UPROPERTY(EditAnywhere, Category = NormalsTopology, meta = (UIMin = "0.0", UIMax = "180.0", ClampMin = "0.0", ClampMax = "180.0", EditCondition = "SplitNormalMethod == ESplitNormalMethod::FaceNormalThreshold"))
	float SharpEdgeAngleThreshold;

	/** Assign separate normals at 'sharp' vertices -- for example, at the tip of a cone */
	UPROPERTY(EditAnywhere, Category = NormalsTopology, meta = (EditCondition = "SplitNormalMethod == ESplitNormalMethod::FaceNormalThreshold"))
	bool bAllowSharpVertices;
};




/**
 * Advanced properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditNormalsAdvancedProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UEditNormalsAdvancedProperties();
};


/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditNormalsOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UEditNormalsTool *Tool;

	int ComponentIndex;

};

/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditNormalsTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	friend UEditNormalsOperatorFactory;

	UEditNormalsTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:

	UPROPERTY()
	UEditNormalsToolProperties* BasicProperties;

	UPROPERTY()
	UEditNormalsAdvancedProperties* AdvancedProperties;


	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;


protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	void UpdateNumPreviews();

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
