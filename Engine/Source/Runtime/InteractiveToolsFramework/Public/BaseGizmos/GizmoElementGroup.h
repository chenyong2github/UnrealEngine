// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/GizmoElementLineBase.h"
#include "InputState.h"
#include "GizmoElementGroup.generated.h"

/**
 * Simple group object intended to be used as part of 3D Gizmos.
 * Contains multiple gizmo objects.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementGroup : public UGizmoElementLineBase
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Add object to group.
	virtual void Add(UGizmoElementBase* InElement);

	// Remove object from group, if it exists
	virtual void Remove(UGizmoElementBase* InElement);

	// Update group and contained elements' visibility state for elements in specified gizmo parts.
	virtual void UpdatePartVisibleState(bool bVisible, uint32 InPartIdentifier);

	// Update group and contained elements' hittable state for elements in specified gizmo parts.
	virtual void UpdatePartHittableState(bool bHittable, uint32 InPartIdentifier);

	// Update group and contained elements' interaction state for elements in specified gizmo parts.
	virtual void UpdatePartInteractionState(EGizmoElementInteractionState InInteractionState, uint32 InPartIdentifier);

	// When true, maintains view-dependent constant scale for this gizmo object hierarchy
	virtual void SetConstantScale(bool InConstantScale);
	virtual bool GetConstantScale() const;

protected:

	// When true, maintains view-dependent constant scale for this gizmo object hierarchy
	UPROPERTY()
	bool bConstantScale = false;

	// When true, this group is treated as a single element such that when LineTrace is called, if any of its sub-elements is hit, 
	// this group will be returned as the owner of the hit. This should be used when a group of elements should be treated as a single handle.
	UPROPERTY()
	bool bHitOwner = false;
		
	// Gizmo elements within this group
	UPROPERTY()
	TArray<TObjectPtr<UGizmoElementBase>> Elements;

	// Updates input transform's scale component to have uniform scale and applies constant scale if bConstantScale is true
	virtual void ApplyUniformConstantScaleToTransform(float PixelToWorldScale, FTransform& InOutLocalToWorldTransform) const;
};
