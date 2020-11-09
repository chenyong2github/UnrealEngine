// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractionMechanic.h"
#include "ToolDataVisualizer.h"
#include "FrameTypes.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Selection/SelectClickedAction.h"

#include "ConstructionPlaneMechanic.generated.h"

class UTransformGizmo;
class UTransformProxy;
class IClickBehaviorTarget;
class USingleClickInputBehavior;

/**
 * UConstructionPlaneMechanic implements an interaction in which a 3D plane can be
 * positioned using the standard 3D Gizmo, or placed at hit-locations in the existing scene.
 * A grid in the plane can optionally be rendered.
 */
UCLASS()
class MODELINGCOMPONENTS_API UConstructionPlaneMechanic : public UInteractionMechanic
{
	GENERATED_BODY()
public:

	/** Replace this to externally control if plane can be updated */
	TUniqueFunction<bool()> CanUpdatePlaneFunc = []() { return true; };

	bool bShowGrid = true;

	FFrame3d Plane;

	DECLARE_MULTICAST_DELEGATE(OnConstructionPlaneChangedEvent);
	OnConstructionPlaneChangedEvent OnPlaneChanged;


public:

	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;

	void Initialize(UWorld* TargetWorld, const FFrame3d& InitialPlane);

	void SetDrawPlaneFromWorldPos(const FVector3d& Position, const FVector3d& Normal, bool bIgnoreNormal);

	/** 
	 * Sets the plane without broadcasting OnPlaneChanged. Useful when the user of the tool wants to change
	 * the plane through some other means. Better than setting the Plane field directly because this function
	 * properly deals with the gizmo.
	 */
	void SetPlaneWithoutBroadcast(const FFrame3d& Plane);

	void SetEnableGridSnaping(bool bEnable);

	void UpdateClickPriority(FInputCapturePriority NewPriority);

public:
	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;
	
	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

	/** 
	 * This is the behavior target used for the Ctrl+click behavior that sets the plane
	 * in the world, exposed here so that the user can modify it after Setup() if needed.
	 * By default, Setup() will have it call SetDrawPlaneFromWorldPos.
	 */
	TUniquePtr<FSelectClickedAction> SetPlaneCtrlClickBehaviorTarget;

protected:
	bool bEnableSnapToWorldGrid = false;

	UPROPERTY()
	USingleClickInputBehavior* ClickToSetPlaneBehavior;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);
};