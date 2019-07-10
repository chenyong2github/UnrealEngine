// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "InputBehaviorSet.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "MeshSurfacePointTool.generated.h"



class UMeshSurfacePointTool;

/**
 * 
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UMeshSurfacePointToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	/** @return true if a single mesh source can be found in the active selection */
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	/** @return new Tool instance initialized with selected mesh source */
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	/** @return new Tool instance. Override this in subclasses to build a different Tool class type */
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const;

	/** Called by BuildTool to configure the Tool with the input MeshSource based on the SceneState */
	virtual void InitializeNewTool(UMeshSurfacePointTool* Tool, const FToolBuilderState& SceneState) const;
};



/**
 * UMeshSurfacePointTool is a base Tool implementation that can be used to implement various
 * "point on surface" interactions. The tool acts on an input IMeshDescriptionSource object,
 * which the standard Builder can extract from the current selection (eg Editor selection).
 * 
 * Subclasses override the OnBeginDrag/OnUpdateDrag/OnEndDrag and OnUpdateHover functions
 * to implement custom behavior.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UMeshSurfacePointTool : public UInteractiveTool, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveTool API Implementation

	/** Register InputBehaviors, etc */
	virtual void Setup() override;


	// UMeshSurfacePointTool API

	/**
	 * Set the Target MeshDescription-providing Object for this tool
	 * @param MeshSource object that can provide a MeshDescriptionSource. Note UMeshSurfacePointTool
	 *        only calls MeshSource->HitTest, so this source technically does not have to actually provide
	 *        a valid MeshDescription
	 */
	virtual void SetMeshSource(TUniquePtr<IMeshDescriptionSource> MeshSource);

	/**
	 * @return true if the target MeshSource is hit by the Ray
	 */
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit);

	
	/**
	 * This function is called by registered InputBehaviors when the user begins a click-drag-release interaction
	 */
	virtual void OnBeginDrag(const FRay& Ray);

	/**
	 * This function is called by registered InputBehaviorseach frame that the user is in a click-drag-release interaction
	 */
	virtual void OnUpdateDrag(const FRay& Ray);

	/**
	 * This function is called by registered InputBehaviors when the user releases the button driving a click-drag-release interaction
	 */
	virtual void OnEndDrag(const FRay& Ray);

	/**
	 * This function is called each frame while the tool is active, to support hover updates
	 */
	virtual void OnUpdateHover(const FInputDeviceRay& DevicePos) {}



	/** Called by registered InputBehaviors to set the state of the "shift" button (or device equivalent) */
	virtual void SetShiftToggle(bool bShiftDown);

	/** @return current state of the shift toggle */
	virtual bool GetShiftToggle() { return bShiftToggle; }


protected:
	/** Target source object that provides a MeshDescription and various other query API calls */
	TUniquePtr<IMeshDescriptionSource> MeshSource;

	/** Current state of the shift modifier toggle */
	bool bShiftToggle;
};








/**
 * UMeshSurfacePointToolMouseBehavior implements mouse press-drag-release interaction behavior for Mouse devices.
 * You can configure the base UAnyButtonInputBehavior to change the mouse button in use (default = left mouse)
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UMeshSurfacePointToolMouseBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	virtual void Initialize(UMeshSurfacePointTool* Tool);

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;

protected:
	UMeshSurfacePointTool* Tool;
	FRay LastWorldRay;
	bool bInDragCapture;
};


