// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "MeshBoundaryToolBase.h"
#include "MeshOpPreviewHelpers.h" //UMeshOpPreviewWithBackgroundCompute
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h"
#include "Properties/MeshMaterialProperties.h"
#include "Properties/RevolveProperties.h"
#include "ToolContextInterfaces.h" // FToolBuilderState

#include "RevolveBoundaryTool.generated.h"

// Tool Builder

UCLASS()
class MESHMODELINGTOOLSEXP_API URevolveBoundaryToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class MESHMODELINGTOOLSEXP_API URevolveBoundaryOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	TObjectPtr<URevolveBoundaryTool> RevolveBoundaryTool;
};

UENUM()
enum class ERevolvePropertiesBoundaryCapFillMode : uint8
{
	/** No caps will be generated. */
	None = static_cast<uint8>(ERevolvePropertiesCapFillMode::None),
	/** Caps are triangulated by placing a vertex in the center and creating a fan to the boundary. This works well if the path is convex,
	  * but can create invalid geometry if it is concave. */
	CenterFan = static_cast<uint8>(ERevolvePropertiesCapFillMode::CenterFan),
	/** Caps are triangulated to maximize the minimal angle in the triangles using Delaunay triangulation. */
	Delaunay = static_cast<uint8>(ERevolvePropertiesCapFillMode::Delaunay),
	/** Caps are triangulated using a standard ear clipping approach. This could result in some very thin triangles. */
	EarClipping = static_cast<uint8>(ERevolvePropertiesCapFillMode::EarClipping)
};

UCLASS()
class MESHMODELINGTOOLSEXP_API URevolveBoundaryToolProperties : public URevolveProperties
{
	GENERATED_BODY()

public:

	/** Determines how end caps are created. This is not relevant if the end caps are not visible or if the path is not closed. */
	UPROPERTY(EditAnywhere, Category = Revolve, AdvancedDisplay, meta = (DisplayAfter = "QuadSplitMode",
		EditCondition = "HeightOffsetPerDegree != 0 || RevolveDegrees != 360"))
	ERevolvePropertiesBoundaryCapFillMode CapFillMode = ERevolvePropertiesBoundaryCapFillMode::Delaunay;

	/** If true, displays the original mesh in addition to the revolved boundary. */
	UPROPERTY(EditAnywhere, Category = Revolve, AdvancedDisplay)
	bool bDisplayInputMesh = false;

	/** Sets the revolution axis origin. */
	UPROPERTY(EditAnywhere, Category = RevolutionAxis, meta = (DisplayName = "Origin"))
	FVector AxisOrigin = FVector(0, 0, 0);

	/** Sets the revolution axis pitch and yaw. */
	UPROPERTY(EditAnywhere, Category = RevolutionAxis, meta = (DisplayName = "Orientation"))
	FVector2D AxisOrientation;

protected:
	virtual ERevolvePropertiesCapFillMode GetCapFillMode() const override
	{
		return static_cast<ERevolvePropertiesCapFillMode>(CapFillMode);
	}
};

/** 
 * Tool that revolves the boundary of a mesh around an axis to create a new mesh. Mainly useful for
 * revolving planar meshes. 
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API URevolveBoundaryTool : public UMeshBoundaryToolBase, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:

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

	UPROPERTY()
	TObjectPtr<URevolveBoundaryToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UNewMeshMaterialProperties> MaterialProperties;

	UPROPERTY()
	TObjectPtr<UConstructionPlaneMechanic> PlaneMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview = nullptr;

	FVector3d RevolutionAxisOrigin;
	FVector3d RevolutionAxisDirection;

	void GenerateAsset(const FDynamicMeshOpResult& Result);
	void UpdateRevolutionAxis();
	void StartPreview();

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	friend class URevolveBoundaryOperatorFactory;
};