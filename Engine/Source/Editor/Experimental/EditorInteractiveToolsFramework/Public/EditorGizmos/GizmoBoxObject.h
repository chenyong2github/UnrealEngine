// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoBoxObject.generated.h"

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a solid axis-aligned 3D box based on parameters.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoBoxObject : public UGizmoBaseObject
{
	GENERATED_BODY()

public:
	//~ Begin UGizmoBaseObject Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit LineTraceObject(const FVector RayStart, const FVector RayDirection) override;
	//~ End UGizmoBaseObject Interface.

public:

	// Dimensions of box
	UPROPERTY(EditAnywhere, Category = Options)
	FVector Dimensions = FVector(20.0f, 20.0f, 20.0f);

	// Direction corresponding to the box's Z-dimension, must be a unit vector
	// This direction is used for view-dependent visibility.
	UPROPERTY(EditAnywhere, Category = Options)
	FVector UpDirection = FVector::ZeroVector;

	// Direction corresponding to the box's Y-dimension, must be a unit vector
	UPROPERTY(EditAnywhere, Category = Options)
	FVector SideDirection = FVector::ZeroVector;

	// Cube center is located at (UpDirection * Offset) 
	UPROPERTY(EditAnywhere, Category = Options)
	float Offset = 0.0f;

protected:

	bool bVisibleViewDependent = true;
};