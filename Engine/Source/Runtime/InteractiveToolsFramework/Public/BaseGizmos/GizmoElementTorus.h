// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoElementTorus.generated.h"

class FPrimitiveDrawInterface;
class FMaterialRenderProxy;

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a torus based on parameters.
 * Note: Ray-torus hit testing is not supported! LineTrace() will never return a hit nor will CalcBounds() calculate box-sphere bounds.
 *       Use a hidden GizmoElementCircle with circle primitive object with DrawLines set to true to approximate 
 *       torus intersection for hit-testing purposes.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementTorus : public UGizmoElementBase
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Torus center.
	virtual void SetCenter(const FVector& InCenter);
	virtual FVector GetCenter() const;

	// Normal to torus.
	virtual void SetNormal(const FVector& InNormal);
	virtual FVector GetNormal() const;

	// Begin axis, should be perpendicular to normal, this where partial torus will begin.
	virtual void SetBeginAxis(const FVector& InBeginAxis);
	virtual FVector GetBeginAxis() const;

	// Outer circle radius.
	virtual void SetOuterRadius(float InOuterRadius);
	virtual float GetOuterRadius() const;

	// Inner circles radius.
	virtual void SetInnerRadius(float InInnerRadius);
	virtual float GetInnerRadius() const;

	// Number of outer segments for rendering torus.
	virtual void SetOuterSegments(int32 InOuterSegments);
	virtual int32 GetOuterSegments() const;

	// Number of inner slices for rendering torus.
	virtual void SetInnerSlices(int32 InInnerSlices);
	virtual int32 GetInnerSlices() const;

	// True when the torus is a partial arc.
	virtual void SetPartial(bool InPartial);
	virtual bool GetPartial() const;

	// True when the partial torus arc should be screen aligned.
	virtual void SetScreenAlignPartial(bool InScreenAlignPartial);
	virtual bool GetScreenAlignPartial() const;

	// If partial, arc angle of partial torus in radians.
	virtual void SetAngle(float InAngle);
	virtual float GetAngle() const;

	// If partial, renders end caps when true.
	virtual void SetEndCaps(bool InEndCaps);
	virtual bool GetEndCaps() const;

protected:

	// Torus center.
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	// Normal to plane in which torus lies.
	UPROPERTY()
	FVector Normal = FVector::UpVector;

	// Plane axis, should be perpendicular to normal. Indicates where partial torus should begin
	UPROPERTY()
	FVector BeginAxis = FVector::ForwardVector;

	// Torus outer radius.
	UPROPERTY()
	float OuterRadius = 100.0f;

	// Torus inner radius.
	UPROPERTY()
	float InnerRadius = 100.0f;

	// Number of segments for rendering torus.
	UPROPERTY()
	int32 OuterSegments = 64;

	// Number of slices to render in each torus segment.
	UPROPERTY()
	int32 InnerSlices = 8;

	// True when the torus is not full.
	UPROPERTY()
	bool bPartial = false;

	// True when partial torus should be aligned to the screen
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bPartial"))
	bool bScreenAlignPartial = false;

	// Angle to render for partial torus
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bPartial"))
	float Angle = 2.0f * PI;

	// Whether to render end caps on a partial torus.
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bPartial"))
	bool bEndCaps = false;
};