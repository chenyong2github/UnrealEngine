// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "GizmoElementLineBase.generated.h"

/**
 * Base class for 2d and 3d primitive objects which support line drawing,
 * intended to be used as part of 3D Gizmos.
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementLineBase : public UGizmoElementBase
{
	GENERATED_BODY()
public:

	// Get line thickness for based on current element interaction state
	virtual float GetCurrentLineThickness() const;

	// Line thickness when rendering lines, 0.0 is valid and will render thinnest line 
	virtual void SetLineThickness(float InLineThickness);
	virtual float GetLineThickness() const;

	// Multiplier applied to line thickness when hovering
	virtual void SetHoverLineThicknessMultiplier(float InHoverLineThicknessMultiplier);
	virtual float GetHoverLineThicknessMultiplier() const;

	// Multiplier applied to line thickness when interacting
	virtual void SetInteractLineThicknessMultiplier(float InInteractLineThicknessMultiplier);
	virtual float GetInteractLineThicknessMultiplier() const;

protected:

	// Line thickness when rendering lines, must be >= 0.0, value of 0.0 will render thinnest line 
	UPROPERTY()
	float LineThickness = 0.0;

	// Multiplier applied to line thickness when hovering
	UPROPERTY(EditAnywhere, Category = Options)
	float HoverLineThicknessMultiplier = 2.0f;

	// Multiplier applied to line thickness when interacting
	UPROPERTY(EditAnywhere, Category = Options)
	float InteractLineThicknessMultiplier = 2.0f;
};