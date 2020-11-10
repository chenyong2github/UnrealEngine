// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "PreviewMesh.h"
#include "Drawing/PreviewGeometryActor.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "MeshTangents.h"
#include "ParameterizationOps/CalculateTangentsOp.h"
#include "MeshTangentsTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshTangentsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshTangentsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Options)
	EMeshTangentsType TangentType = EMeshTangentsType::FastMikkTSpace;


	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowTangents = true;

	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowNormals = false;

	UPROPERTY(EditAnywhere, Category = Visualization, AdvancedDisplay)
	bool bHideDegenerates = true;

	UPROPERTY(EditAnywhere, Category = Visualization, AdvancedDisplay, meta = (UIMin = "0.01", UIMax = "25.0", ClampMin = "0.01", ClampMax = "10000000.0"))
	float LineLength = 2.0;

	UPROPERTY(EditAnywhere, Category = Visualization, AdvancedDisplay, meta = (UIMin = "0", UIMax = "25.0", ClampMin = "0", ClampMax = "1000.0"))
	float LineThickness = 3.0;


	UPROPERTY(EditAnywhere, Category = Visualization, AdvancedDisplay)
	bool bCompareWithMikkt = false;

	UPROPERTY(EditAnywhere, Category = Visualization, AdvancedDisplay, meta = (UIMin = "0.5", UIMax = "90.0"))
	float AngleThreshDeg = 5.0;

};





/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshTangentsTool : public USingleSelectionTool, public IGenericDataOperatorFactory<FMeshTangentsd>
{
	GENERATED_BODY()

public:
	UMeshTangentsTool();

	virtual void SetWorld(UWorld* World);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// IGenericDataOperatorFactory API
	virtual TUniquePtr<TGenericDataOperator<FMeshTangentsd>> MakeNewOperator() override;

protected:
	UPROPERTY()
	UMeshTangentsToolProperties* Settings = nullptr;

protected:
	UPROPERTY()
	UMaterialInterface* DefaultMaterial = nullptr;

	UPROPERTY()
	UPreviewMesh* PreviewMesh = nullptr;

	UPROPERTY()
	UPreviewGeometry* PreviewGeometry = nullptr;

	TUniquePtr<TGenericDataBackgroundCompute<FMeshTangentsd>> Compute = nullptr;

protected:
	UWorld* TargetWorld;

	TSharedPtr<FMeshDescription> InputMeshDescription;
	TSharedPtr<FMeshTangentsf> InitialTangents;
	TSharedPtr<FDynamicMesh3> InputMesh;

	bool bThicknessDirty = false;
	bool bLengthDirty = false;
	bool bVisibilityChanged = false;

	void OnTangentsUpdated(const TUniquePtr<FMeshTangentsd>& NewResult);
	void UpdateVisualization(bool bThicknessChanged, bool bLengthChanged);

	struct FMikktDeviation
	{
		float MaxAngleDeg;
		int32 TriangleID;
		int32 TriVertIndex;
		FVector3d VertexPos;
		FVector3d MikktTangent, MikktBitangent;
		FVector3d OtherTangent, OtherBitangent;
	};
	TArray<FMikktDeviation> Deviations;

};
