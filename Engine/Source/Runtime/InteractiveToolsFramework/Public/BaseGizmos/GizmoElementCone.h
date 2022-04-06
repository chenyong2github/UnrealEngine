// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoElementCone.generated.h"

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a solid 3D cone based on parameters.
 */

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementCone : public UGizmoElementBase
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Location of center of cone's base circle.
	virtual void SetBase(const FVector& InBase);
	virtual FVector GetBase() const;

	// Cone axis direction.
	virtual void SetDirection(const FVector& InDirection);
	virtual FVector GetDirection() const;

	// Cone height.
	virtual void SetHeight(float InHeight);
	virtual float GetHeight() const;

	// Cone radius.
	virtual void SetRadius(float InRadius);
	virtual float GetRadius() const;

	// Number of sides to use when rendering cone.
	virtual void SetNumSides(int32 InNumSides);
	virtual int32 GetNumSides() const;

protected:

	// Location of center of cone's base circle.
	UPROPERTY()
	FVector Base = FVector::ZeroVector;

	// Direction of cone's axis.
	UPROPERTY()
	FVector Direction = FVector(0.0f, 0.0f, 1.0f);

	// Height of cone
	UPROPERTY()
	float Height = 1.0f;

	// Radius of cone circle.
	UPROPERTY()
	float Radius = 0.5f;

	// Number of sides for tessellating cone
	UPROPERTY()
	int32 NumSides = 32;
};

