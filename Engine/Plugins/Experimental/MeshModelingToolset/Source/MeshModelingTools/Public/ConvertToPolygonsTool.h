// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "FindPolygonsAlgorithm.h"
#include "PreviewMesh.h"
#include "ConvertToPolygonsTool.generated.h"

// predeclaration
struct FMeshDescription;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState & SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState & SceneState) const override;
};




UENUM()
enum class EConvertToPolygonsMode
{
	/** Convert based on Angle Tolerance between Face Normals */
	FaceNormalDeviation,
	/** Create PolyGroups based on UV Islands */
	FromUVISlands
};



UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Strategy to use to group triangles */
	UPROPERTY(EditAnywhere, Category = PolyGroups)
	EConvertToPolygonsMode ConversionMode = EConvertToPolygonsMode::FaceNormalDeviation;

	/** Tolerance for planarity */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (UIMin = "0.001", UIMax = "20.0", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation"))
	float AngleTolerance = 0.1f;

	/** If true, normals are recomputed per-group, with hard edges at group boundaries */
	UPROPERTY(EditAnywhere, Category = PolyGroups)
	bool bCalculateNormals = true;
	
	/** Display each group with a different auto-generated color */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowGroupColors = true;
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	UConvertToPolygonsTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:
	UPROPERTY()
	UConvertToPolygonsToolProperties* Settings;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

protected:
	FDynamicMesh3 SearchMesh;
	FDynamicMeshNormalOverlay InitialNormals;

	FFindPolygonsAlgorithm Polygons;
	bool bPolygonsValid = false;
	void UpdatePolygons();

	void UpdateVisualization();

	void ConvertToPolygons(FMeshDescription* MeshIn);

};
