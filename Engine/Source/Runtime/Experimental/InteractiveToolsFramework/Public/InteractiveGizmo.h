// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehaviorSet.h"
#include "ToolContextInterfaces.h"
#include "InteractiveGizmo.generated.h"

class UInteractiveGizmoManager;



/**
 * UInteractiveGizmo is the base class for all Gizmos in the InteractiveToolsFramework.
 *
 * @todo callback/delegate for if/when .InputBehaviors changes
 * @todo callback/delegate for when Gizmo properties change
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveGizmo : public UObject, public IInputBehaviorSource
{
	GENERATED_BODY()

public:
	UInteractiveGizmo();

	/**
	 * Called by GizmoManager to initialize the Gizmo *after* GizmoBuilder::BuildGizmo() has been called
	 */
	virtual void Setup();

	/**
	 * Called by GizmoManager to shut down the Gizmo
	 */
	virtual void Shutdown();

	/**
	 * Allow the Gizmo to do any custom drawing (ie via PDI/RHI)
	 * @param RenderAPI Abstraction that provides access to Rendering in the current ToolsContext
	 */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);

	/**
	 * Allow the Gizmo to do any necessary processing on Tick 
	 * @param DeltaTime the time delta since last tick
	 */
	virtual void Tick(float DeltaTime);



	/**
	 * @return GizmoManager that owns this Gizmo
	 */
	virtual UInteractiveGizmoManager* GetGizmoManager() const;



	//
	// Input Behaviors support
	//

	/**
	 * Add an input behavior for this Gizmo
	 * @param Behavior behavior to add
	 */
	virtual void AddInputBehavior(UInputBehavior* Behavior);

	/**
	 * @return Current input behavior set.
	 */
	virtual const UInputBehaviorSet* GetInputBehaviors() const;



protected:

	/** The current set of InputBehaviors provided by this Gizmo */
	UPROPERTY()
	UInputBehaviorSet* InputBehaviors;
};



/**
 * ETransformGizmoSubElements identifies the sub-elements of a standard 3-axis transformation Gizmo.
 * Used by GizmoManager to customize UTransformGizmo instances.
 */
UENUM()
enum class ETransformGizmoSubElements
{
	None = 0,

	TranslateAxisX = 1,
	TranslateAxisY = 2,
	TranslateAxisZ = 4,
	TranslateAllAxes = TranslateAxisX | TranslateAxisY | TranslateAxisZ,

	TranslatePlaneXY = 8,
	TranslatePlaneXZ = 16,
	TranslatePlaneYZ = 32,
	TranslateAllPlanes = TranslatePlaneXY | TranslatePlaneXZ | TranslatePlaneYZ,

	RotateAxisX = 64,
	RotateAxisY = 128,
	RotateAxisZ = 256,
	RotateAllAxes = RotateAxisX | RotateAxisY | RotateAxisZ,

	StanedardTranslateRotate = TranslateAllAxes | TranslateAllPlanes | RotateAllAxes
};
ENUM_CLASS_FLAGS(ETransformGizmoSubElements);