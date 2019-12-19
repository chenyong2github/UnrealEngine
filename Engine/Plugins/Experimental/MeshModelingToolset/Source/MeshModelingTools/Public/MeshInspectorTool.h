// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Properties/MeshMaterialProperties.h"
#include "MeshInspectorTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UMaterialInstanceDynamic;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshInspectorToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UCLASS()
class MESHMODELINGTOOLS_API UMeshInspectorProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Toggle visibility of all mesh edges */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bWireframe = true;

	/** Toggle visibility of open boundary edges */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bBoundaryEdges = true;

	/** Toggle visibility of polygon borders */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bPolygonBorders = false;

	/** Toggle visibility of UV seam edges */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bUVSeams = false;

	/** Toggle visibility of Normal seam edges */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bNormalSeams = false;

	/** Toggle visibility of normal vectors */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bNormalVectors = false;

	/** Toggle visibility of tangent vectors */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bTangentVectors = false;

	/** Length of line segments representing normal vectors */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bNormalVectors"))
	float NormalLength = 5.0f;

	/** Length of line segments representing tangent vectors */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bTangentVectors"))
	float TangentLength = 5.0f;


	//
	// save/restore support
	//
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};




/**
 * Mesh Inspector Tool for visualizing mesh information
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshInspectorTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	UMeshInspectorTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

public:

	virtual void IncreaseLineWidthAction();
	virtual void DecreaseLineWidthAction();

protected:

	UPROPERTY()
	UMeshInspectorProperties* Settings = nullptr;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;


	float LineWidthMultiplier = 1.0f;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UMaterialInterface* DefaultMaterial = nullptr;

	TArray<int> BoundaryEdges;
	TArray<int> UVSeamEdges;
	TArray<int> NormalSeamEdges;
	TArray<int> GroupBoundaryEdges;
	void Precompute();
};
