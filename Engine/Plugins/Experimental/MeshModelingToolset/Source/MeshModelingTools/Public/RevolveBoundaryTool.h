// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "SingleSelectionTool.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "MeshBoundaryToolBase.h"
#include "MeshOpPreviewHelpers.h" //UMeshOpPreviewWithBackgroundCompute
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h" //UInteractiveToolBuilder
#include "Properties/MeshMaterialProperties.h"
#include "Properties/RevolveProperties.h"
#include "ToolContextInterfaces.h" // FToolBuilderState

#include "RevolveBoundaryTool.generated.h"

class IAssetGenerationAPI;

// Tool Builder

UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;

	bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	URevolveBoundaryTool* RevolveBoundaryTool;
};


UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryToolProperties : public URevolveProperties
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = RevolutionAxis)
	FTransform RevolutionAxis = FTransform(FRotator(90, 0, 0));

	UPROPERTY(EditAnywhere, Category = RevolutionAxis)
	bool bSnapToWorldGrid = false;

	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bDisplayOriginalMesh = false;
};


/** 
 * Tool that revolves the boundary of a mesh around an axis to create a new mesh. Mainly useful for
 * revolving planar meshes. 
 */
UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryTool : public UMeshBoundaryToolBase
{
	GENERATED_BODY()

public:
	virtual void SetAssetAPI(IAssetGenerationAPI* NewAssetApi) { AssetAPI = NewAssetApi; }

	virtual bool CanAccept() const override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
protected:

	// Support for Ctrl+Clicking a boundary to align the revolution axis to that segment
	bool bAlignAxisOnClick = false;
	int32 AlignAxisModifier = 2;

	IAssetGenerationAPI* AssetAPI = nullptr;

	UPROPERTY()
	URevolveBoundaryToolProperties* Settings = nullptr;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;

	UPROPERTY()
	UConstructionPlaneMechanic* PlaneMechanic = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	FVector3d RevolutionAxisOrigin;
	FVector3d RevolutionAxisDirection;

	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;
	virtual bool ShouldSelectionAppend() const override { return false; }

	void GenerateAsset(const FDynamicMeshOpResult& Result);
	void UpdateRevolutionAxis(const FTransform& PlaneTransform);
	void StartPreview();

	friend class URevolveBoundaryOperatorFactory;
};