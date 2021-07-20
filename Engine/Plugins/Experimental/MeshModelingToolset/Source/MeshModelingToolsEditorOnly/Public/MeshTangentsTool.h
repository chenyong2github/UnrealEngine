// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "PreviewMesh.h"
#include "Drawing/PreviewGeometryActor.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTangents.h"
#include "ParameterizationOps/CalculateTangentsOp.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "MeshTangentsTool.generated.h"


// predeclarations
struct FMeshDescription;
class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshTangentsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
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
class MESHMODELINGTOOLSEDITORONLY_API UMeshTangentsTool : public USingleSelectionMeshEditingTool, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshTangentsd>
{
	GENERATED_BODY()
public:
	UMeshTangentsTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// IGenericDataOperatorFactory API
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshTangentsd>> MakeNewOperator() override;

protected:
	UPROPERTY()
	TObjectPtr<UMeshTangentsToolProperties> Settings = nullptr;

protected:
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultMaterial = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewGeometry> PreviewGeometry = nullptr;

	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshTangentsd>> Compute = nullptr;

protected:
	TSharedPtr<FMeshDescription, ESPMode::ThreadSafe> InputMeshDescription;
	TSharedPtr<UE::Geometry::FMeshTangentsf, ESPMode::ThreadSafe> InitialTangents;
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;

	bool bThicknessDirty = false;
	bool bLengthDirty = false;
	bool bVisibilityChanged = false;

	void OnTangentsUpdated(const TUniquePtr<UE::Geometry::FMeshTangentsd>& NewResult);
	void UpdateVisualization(bool bThicknessChanged, bool bLengthChanged);

	struct FMikktDeviation
	{
		float MaxAngleDeg;
		int32 TriangleID;
		int32 TriVertIndex;
		FVector3f VertexPos;
		FVector3f MikktTangent, MikktBitangent;
		FVector3f OtherTangent, OtherBitangent;
	};
	TArray<FMikktDeviation> Deviations;

};
