// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolObjects.h"
#include "GizmoActor.generated.h"

class UGizmoArrowComponent;
class UGizmoRectangleComponent;
class UGizmoCircleComponent;


/**
 * AGizmoActor is a base class for Actors that would be created to represent Gizmos in the scene.
 * Currently this does not involve any special functionality, but a set of static functions
 * are provided to create default Components commonly used in Gizmos.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API AGizmoActor : public AInternalToolFrameworkActor
{
	GENERATED_BODY()
public:
	AGizmoActor();

	/** Add standard arrow component to Actor, generally used for axis-translation */
	static UGizmoArrowComponent* AddDefaultArrowComponent(
		UWorld* World, AActor* Actor,
		const FLinearColor& Color, const FVector& LocalDirection
	);
	/** Add standard rectangle component to Actor, generally used for plane-translation */
	static UGizmoRectangleComponent* AddDefaultRectangleComponent(
		UWorld* World, AActor* Actor,
		const FLinearColor& Color, const FVector& PlaneAxis1, const FVector& PlaneAxisx2
	);
	/** Add standard circle component to Actor, generally used for axis-rotation */
	static UGizmoCircleComponent* AddDefaultCircleComponent(
		UWorld* World, AActor* Actor,
		const FLinearColor& Color, const FVector& PlaneNormal
	);
};
