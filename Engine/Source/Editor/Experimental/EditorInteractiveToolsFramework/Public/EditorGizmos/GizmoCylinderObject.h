// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoCylinderObject.generated.h"

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a solid 3D cylinder based on parameters.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoCylinderObject : public UGizmoBaseObject
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoBaseObject Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit LineTraceObject(const FVector RayOrigin, const FVector RayDirection) override;
	//~ End UGizmoBaseObject Interface.

public:

	// Orientation of cylinder's axis, must be a unit-vector
	// This direction will also be used for view-dependent culling.
	UPROPERTY(EditAnywhere, Category = Options)
	FVector Direction = FVector(1.0, 0.0, 0.0);

	// Length
	UPROPERTY(EditAnywhere, Category = Options)
	double Length = 1.0;

	// Base of cylinder is located at (Direction * Offset)
	UPROPERTY(EditAnywhere, Category = Options)
	double Offset = 0.0;

	// Radius
	UPROPERTY(EditAnywhere, Category = Options)
	double Radius = 1.2;

	// Number of sides for tessellating cylinder
	UPROPERTY(EditAnywhere, Category = Options)
	int NumSides = 16;

protected:

	bool bVisibleViewDependent = true;
};