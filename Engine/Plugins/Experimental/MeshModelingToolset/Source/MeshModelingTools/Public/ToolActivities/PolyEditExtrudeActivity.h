// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveToolActivity.h"
#include "FrameTypes.h"

#include "PolyEditExtrudeActivity.generated.h"

class UPolyEditActivityContext;
class UPolyEditPreviewMesh;
class UPlaneDistanceFromHitMechanic;

UENUM()
enum class EPolyEditExtrudeMode
{
	//~ TODO: this is what we actually want, but not yet implemented.
	//~ SelectedFaceNormals,

	SingleDirection,

	//~ These are likely not very useful, but they can sometimes take
	//~ the place of SelectedFaceNormals, until we implement that.
	SelectedTriangleNormals,
	VertexNormals,
};

UENUM()
enum class EPolyEditExtrudeDirection
{
	SelectionNormal,
	WorldX,
	WorldY,
	WorldZ,
	LocalX,
	LocalY,
	LocalZ
};


UCLASS()
class MESHMODELINGTOOLS_API UPolyEditExtrudeProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Extrude)
	EPolyEditExtrudeMode ExtrudeMode = EPolyEditExtrudeMode::SingleDirection;

	UPROPERTY(EditAnywhere, Category = Extrude)
	EPolyEditExtrudeDirection Direction = EPolyEditExtrudeDirection::SelectionNormal;

	/** Controls whether extruding an entire open-border patch should create a solid or an open shell */
	UPROPERTY(EditAnywhere, Category = Extrude)
	bool bShellsToSolids = true;
};

/**
 * 
 */
UCLASS()
class MESHMODELINGTOOLS_API UPolyEditExtrudeActivity : public UInteractiveToolActivity, 
	public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// IInteractiveToolActivity
	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual bool CanStart() const override;
	virtual EToolActivityStartResult Start() override;
	virtual bool IsRunning() const override { return bIsRunning; }
	virtual bool CanAccept() const override;
	virtual EToolActivityEndResult End(EToolShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override {}

	UPROPERTY()
	UPolyEditExtrudeProperties* ExtrudeProperties;

protected:
	FVector3d GetExtrudeDirection() const;
	void BeginExtrude();
	void ApplyExtrude();
	void Clear();

	UPROPERTY()
	UPolyEditPreviewMesh* EditPreview;

	UPROPERTY()
	UPlaneDistanceFromHitMechanic* ExtrudeHeightMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UPolyEditActivityContext> ActivityContext;

	bool bIsRunning = false;

	UE::Geometry::FFrame3d ActiveSelectionFrameWorld;
	float UVScaleFactor = 1.0f;
	bool bPreviewUpdatePending = false;
};
