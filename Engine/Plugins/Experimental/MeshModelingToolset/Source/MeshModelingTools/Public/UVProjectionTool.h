// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolDataVisualizer.h"
#include "ParameterizationOps/UVProjectionOp.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "Properties/MeshMaterialProperties.h"

#include "UVProjectionTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UTransformGizmo;
class UTransformProxy;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolBuilder : public UInteractiveToolBuilder
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
class MESHMODELINGTOOLS_API UUVProjectionToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UUVProjectionToolProperties();



	/** Choose the UV projection method (cube, cylinder, plane) */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	EUVProjectionMethod UVProjectionMethod;

	/** Per-axis scaling of projection primitive */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	FVector ProjectionPrimitiveScale;

	/** If triangle normal direction is within this threshold degrees of the cylinder top/bottom plane direction, project UVs to the top/bottom plane instead of the sides */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings, meta = (Default = "1", UIMin = "0", UIMax = "20", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "UVProjectionMethod == EUVProjectionMethod::Cylinder"))
	float CylinderProjectToTopOrBottomAngleThreshold;

	/** Choose the UV scale factors */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	FVector2D UVScale;

	/** Choose the UV offsets */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	FVector2D UVOffset;

	/** If set, UV scales will be relative to world space so different objects created with the same UV scale should have the same average texel size */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings, meta = (DisplayName = "UV Scale Relative to World Space"))
	bool bWorldSpaceUVScale = false;
};




/**
 * Advanced properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionAdvancedProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UUVProjectionAdvancedProperties();
};


/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UUVProjectionTool *Tool;

	int ComponentIndex;

};

/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	friend UUVProjectionOperatorFactory;

	UUVProjectionTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:

	UPROPERTY()
	UUVProjectionToolProperties* BasicProperties;

	UPROPERTY()
	UUVProjectionAdvancedProperties* AdvancedProperties;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;



	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;


	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;

	UPROPERTY()
	TArray<UTransformGizmo*> TransformGizmos;
	
	UPROPERTY()
	TArray<UTransformProxy*> TransformProxies;

protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;
	TArray<FDynamicMesh3> ReferencePrimitives;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;
	UInteractiveGizmoManager* GizmoManager;

	FViewCameraState CameraState;

	FToolDataVisualizer ProjectionShapeVisualizer;

	void UpdateNumPreviews();
	void UpdateVisualization();

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);
};
