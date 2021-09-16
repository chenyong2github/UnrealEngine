// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "ToolContextInterfaces.h"
#include "GizmoBaseObject.generated.h"

/**
 * Base class for simple objects intended to be used as part of 3D Gizmos.
 * Contains common properties and utility functions.
 * This class does nothing by itself, use subclasses like UGizmoCylinderObject
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoBaseObject : public UObject
{
	GENERATED_BODY()

public:

	virtual void Render(IToolsContextRenderAPI* RenderAPI) {}
	virtual FInputRayHit LineTraceObject(const FVector Start, const FVector Direction) { return FInputRayHit(); }

public:

	UPROPERTY(EditAnywhere, Category = Options)
	FTransform LocalToWorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, Category = Options)
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = Options)
	TObjectPtr<UMaterialInterface> CurrentMaterial;


	UPROPERTY(EditAnywhere, Category = Options)
	float GizmoScale;

	// @todo need this?
	UPROPERTY(EditAnywhere, Category = Options)
	float PixelHitDistanceThreshold = 7.0f;

	// @todo add view dependent controls
	// UPROPERTY(EditAnywhere, Category = Options)
	// uint8 bEnableViewDependentVisibility

	// angle in radians
	// UPROPERTY(EditAnywhere, Category = Options)
	// float ViewDependentVisibilityAngle

	// UPROPERTY(EditAnywhere, Category = Options)
	// uint8 bEnableViewDependentScaling


public:

	UFUNCTION()
	virtual void SetHoverState(bool bHoveringIn)
	{
		bHovering = bHoveringIn;
	}

	UFUNCTION()
	virtual bool GetHoverState()
	{
		return bHovering;
	}

	UFUNCTION()
	virtual void SetInteractingState(bool bInteractingIn)
	{
		bInteracting = bInteractingIn;
	}

	UFUNCTION()
		virtual bool GetInteractingState()
	{
		return bInteracting;
	}

	UFUNCTION()
	virtual void SetWorldLocalState(bool bWorldIn)
	{
		bWorld = bWorldIn;
	}

	UFUNCTION()
	virtual bool GetWorldLocalState()
	{
		return bWorld;
	}

	UFUNCTION()
	virtual void SetVisibility(bool bVisibleIn)
	{
		bVisible = bVisibleIn;
	}

	UFUNCTION()
	virtual bool GetVisibility()
	{
		return bVisible;
	}

	UFUNCTION()
	virtual void SetLocalToWorldTransform(const FTransform LocalToWorldTransformIn)
	{
		LocalToWorldTransform = LocalToWorldTransformIn;
	}

	UFUNCTION()
	virtual FTransform GetLocalToWorldTransform()
	{
		return LocalToWorldTransform;
	}

	UFUNCTION()
	virtual void SetGizmoScale(float InGizmoScale)
	{
		GizmoScale = InGizmoScale;
	}

	UFUNCTION()
	virtual float GetGizmoScale()
	{
		return GizmoScale;
	}

	UFUNCTION()
	virtual void SetMaterial(UMaterialInterface* InMaterial)
	{
		Material = InMaterial;
	}

	UFUNCTION()
	virtual UMaterialInterface* GetMaterial()
	{
		return Material;
	}

	UFUNCTION()
	virtual void SetCurrentMaterial(UMaterialInterface* InCurrentMaterial)
	{
		CurrentMaterial = InCurrentMaterial;
	}

	UFUNCTION()
	virtual UMaterialInterface* GetCurrentMaterial()
	{
		return CurrentMaterial;
	}

protected:
	// scale factor between pixel distances and world distances at Gizmo origin
	float DynamicPixelToWorldScale = 1.0f;

	// hover state
	bool bHovering = false;

	// interacting state
	bool bInteracting = false;

	// world/local coordinates state
	bool bWorld = false;

	// visibility state
	bool bVisible = true;
};
