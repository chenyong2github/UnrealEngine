// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoConeObject.generated.h"

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a solid 3D cone based on parameters.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoConeObject : public UGizmoBaseObject
{
	GENERATED_BODY()

public:
	//~ Begin UGizmoBaseObject Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit LineTraceObject(const FVector Start, const FVector Direction) override;
	//~ End UGizmoBaseObject Interface.

public:

	// Direction of cone's axis, must be a unit vector
	// This direction will also be used for view-dependent culling.
	UPROPERTY(EditAnywhere, Category = Options)
	FVector Direction = FVector(0.0f, 0.0f, 1.0f);

	// Height of cone
	UPROPERTY(EditAnywhere, Category = Options)
	double Height = 1.0f;

	// Cone's point is located at (Direction * Offset)
	// or the origin if Offset is 0
	UPROPERTY(EditAnywhere, Category = Options)
	double Offset = 0.0f;

	// Angle in radians between cone's axis and slant edge
	UPROPERTY(EditAnywhere, Category = Options)
	double Angle = 0.274f;

	// Number of sides for tessellating cone
	UPROPERTY(EditAnywhere, Category = Options)
	int NumSides = 32;

protected:

	bool bVisibleViewDependent = true;
};