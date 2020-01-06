// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "CuttingOps/EmbedPolygonsOp.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "PolygonOnMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UTransformGizmo;
class UTransformProxy;




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Standard properties of the polygon-on-mesh operations
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPolygonOnMeshToolProperties();

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes;

	/** What operation to do with the polygon -- e.g. extrude it, cut through with it, or delete inside it */
	UPROPERTY(EditAnywhere, Category = Options)
	EEmbeddedPolygonOpMethod PolygonOperation;

	// TODO: re-add if/when extrude is added as a supported operation
	///** Amount to extrude, if extrude is enabled */
	//UPROPERTY(EditAnywhere, Category = Options)
	//float ExtrudeDistance;

	/** Scale of polygon to embed */
	UPROPERTY(EditAnywhere, Category = Options)
	float PolygonScale;
};




/**
 * Advanced properties of the plane cut operation
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshAdvancedProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPolygonOnMeshAdvancedProperties();

	// TODO: add additional properties, e.g. perhaps more advanced triangulation options, perhaps post-process remeshing options (because the polygon-in-mesh embedding can create slivers)

};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UPolygonOnMeshTool *Tool;

	// TODO: add info about what this factory is specifically meant to do with the tool (e.g. cut inside vs outside)
	// currently not needed as we only do one thing at a time, as configured by the tool properties
};

/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:

	friend UPolygonOnMeshOperatorFactory;

	UPolygonOnMeshTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

protected:

	UPROPERTY()
	UPolygonOnMeshToolProperties* BasicProperties;

	UPROPERTY()
	UPolygonOnMeshAdvancedProperties* AdvancedProperties;

	/** Origin of polygon projection plane */
	UPROPERTY()
	FVector EmbedPolygonOrigin;

	/** Orientation of polygon projection plane */
	UPROPERTY()
	FQuat EmbedPolygonOrientation;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;


protected:
	TSharedPtr<FDynamicMesh3> OriginalDynamicMesh;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	void UpdateNumPreviews();

	IClickBehaviorTarget* SetPointInWorldConnector = nullptr;

	virtual void SetPlaneFromWorldPos(const FVector& Position, const FVector& Normal);

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
