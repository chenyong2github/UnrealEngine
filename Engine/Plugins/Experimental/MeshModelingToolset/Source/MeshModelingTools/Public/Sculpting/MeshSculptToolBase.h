// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseTools/BaseBrushTool.h"
#include "Sculpting/MeshBrushOpBase.h"
#include "BoxTypes.h"
#include "Properties/MeshMaterialProperties.h"
#include "Changes/ValueWatcher.h"
#include "MeshSculptToolBase.generated.h"


class UMaterialInstanceDynamic;
class UTransformGizmo;
class UTransformProxy;
class UPreviewMesh;
class UBaseDynamicMeshComponent;


UCLASS()
class MESHMODELINGTOOLS_API USculptBrushProperties : public UBrushBaseProperties
{
	GENERATED_BODY()
public:
	USculptBrushProperties()
	{
		this->BrushFalloffAmount = 0.5f;
	}

	/** Depth of Brush into surface along view ray or surface normal, depending on the Active Brush Type */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0", DisplayPriority = 5))
	float Depth = 0;

	/** Allow the Brush to hit the back-side of the mesh */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (DisplayPriority = 6))
	bool bHitBackFaces = true;
};




UCLASS()
class MESHMODELINGTOOLS_API UKelvinBrushProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Brush Fall off as fraction of brush size*/
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", ClampMin = "0.0"))
	float FallOffDistance = 1.f;

	/** How much the mesh resists shear */
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", ClampMin = "0.0"))
	float Stiffness = 1.f;

	/** How compressible the spatial region is: 1 - 2 x Poisson ratio */
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", UIMax = "0.1", ClampMin = "0.0", ClampMax = "1.0"))
	float Incompressiblity = 1.f;

	/** Integration steps*/
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", UIMax = "100", ClampMin = "0.0", ClampMax = "100"))
	int BrushSteps = 3;
};



UENUM()
enum class EPlaneBrushSideMode : uint8
{
	BothSides = 0,
	PushDown = 1,
	PullTowards = 2
};



UCLASS()
class MESHMODELINGTOOLS_API UPlaneBrushProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Control whether effect of brush should be limited to one side of the Plane  */
	UPROPERTY(EditAnywhere, Category = PlaneBrush)
	EPlaneBrushSideMode WhichSide = EPlaneBrushSideMode::BothSides;
};



UCLASS()
class MESHMODELINGTOOLS_API UWorkPlaneProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY( meta = (TransientToolProperty) )
	bool bPropertySetEnabled = true;

	/** Toggle whether Work Plane Positioing Gizmo is visible */
	UPROPERTY(EditAnywhere, Category = TargetPlane, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	bool bShowGizmo = true;

	/** Toggle whether Work Plane snaps to grid when using Gizmo */
	UPROPERTY(EditAnywhere, Category = TargetPlane, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	bool bSnapToGrid = true;

	UPROPERTY(EditAnywhere, Category = TargetPlane, AdvancedDisplay, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = TargetPlane, AdvancedDisplay, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	FQuat Rotation = FQuat::Identity;
};




UCLASS()
class MESHMODELINGTOOLS_API USculptMaxBrushProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Specify maximum displacement height (relative to brush size) */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxHeight = 0.5;

	/** Use maximum height from last brush stroke, regardless of brush size. Note that spatial brush falloff still applies.  */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush)
	bool bFreezeCurrentHeight = false;
};





/**
 * Base Tool for mesh sculpting tools, provides some shared functionality
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSculptToolBase : public UMeshSurfacePointTool
{
	GENERATED_BODY()
public:
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void SetWorld(UWorld* World);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

protected:
	virtual void OnTick(float DeltaTime) override;

protected:
	UWorld* TargetWorld;		// required to spawn UPreviewMesh/etc
	FViewCameraState CameraState;

	/**
	 * Subclass must implement this and return relevant rendering component
	 */
	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() { check(false); return nullptr; }


public:
	/** Properties that control brush size/etc*/
	UPROPERTY()
	USculptBrushProperties* BrushProperties;

	UPROPERTY()
	UWorkPlaneProperties* GizmoProperties;


	//
	// Brush Size
	//
protected:
	FInterval1d BrushRelativeSizeRange;
	double CurrentBrushRadius;
	virtual void InitializeBrushSizeRange(const FAxisAlignedBox3d& TargetBounds);
	virtual void CalculateBrushRadius();
	virtual double GetCurrentBrushRadius() const { return CurrentBrushRadius; }

	double ActivePressure = 1.0;
	virtual double GetActivePressure() const { return ActivePressure; }

public:
	virtual void IncreaseBrushRadiusAction();
	virtual void DecreaseBrushRadiusAction();
	virtual void IncreaseBrushRadiusSmallStepAction();
	virtual void DecreaseBrushRadiusSmallStepAction();
	virtual void IncreaseBrushFalloffAction();
	virtual void DecreaseBrushFalloffAction();


	// client currently needs to implement these...
	virtual void IncreaseBrushSpeedAction() {}
	virtual void DecreaseBrushSpeedAction() {}
	virtual void NextBrushModeAction() {}
	virtual void PreviousBrushModeAction() {}


	//
	// Stamps
	//
protected:
	FSculptBrushStamp HoverStamp;
	FSculptBrushStamp CurrentStamp;
	FSculptBrushStamp LastStamp;
	void UpdateHoverStamp(const FVector3d& WorldPos, const FVector3d& WorldNormal);




	//
	// Display / Material
	//
public:
	UPROPERTY()
	UMeshEditingViewProperties* ViewProperties;

	UPROPERTY()
	UMaterialInstanceDynamic* ActiveOverrideMaterial;

protected:
	virtual void UpdateWireframeVisibility(bool bNewValue);
	virtual void UpdateMaterialMode(EMeshEditingMaterialModes NewMode);
	virtual void UpdateFlatShadingSetting(bool bNewValue);
	virtual void UpdateColorSetting(FLinearColor NewColor);
	virtual void UpdateImageSetting(UTexture2D* NewImage);



	//
	// brush indicator
	//
protected:
	// subclasses should call this to create indicator in their ::Setup()
	virtual void InitializeIndicator();

protected:
	UPROPERTY()
	UBrushStampIndicator* BrushIndicator;

	UPROPERTY()
	UMaterialInstanceDynamic* BrushIndicatorMaterial;

	UPROPERTY()
	UPreviewMesh* BrushIndicatorMesh;

	// creates default sphere indicator
	UPreviewMesh* MakeDefaultIndicatorSphereMesh(UObject* Parent, UWorld* World, int Resolution = 32);




	//
	// Work Plane
	//
public:
	// plane gizmo
	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

protected:
	virtual void UpdateWorkPlane();
	virtual bool ShowWorkPlane() const { return false; };

private:
	TValueWatcher<FVector> GizmoPositionWatcher;
	TValueWatcher<FQuat> GizmoRotationWatcher;

	void UpdateGizmoFromProperties();
	void PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform);

	enum class EPendingWorkPlaneUpdate
	{
		NoUpdatePending,
		MoveToHitPositionNormal,
		MoveToHitPosition,
		MoveToHitPositionViewAligned
	};
	EPendingWorkPlaneUpdate PendingWorkPlaneUpdate;
	void SetFixedSculptPlaneFromWorldPos(const FVector& Position, const FVector& Normal, EPendingWorkPlaneUpdate UpdateType);
	void UpdateFixedSculptPlanePosition(const FVector& Position);
	void UpdateFixedSculptPlaneRotation(const FQuat& Rotation);
	void UpdateFixedPlaneGizmoVisibility(bool bVisible);

};