// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "EditorGizmos/GizmoBoxObject.h"
#include "EditorGizmos/GizmoConeObject.h"
#include "EditorGizmos/GizmoCylinderObject.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoArrowObject.generated.h"

/**
 * Simple arrow object intended to be used as part of 3D Gizmos.
 * Draws a solid 3D arrow based with a cylinder body and
 * cone or box head.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoArrowObject : public UGizmoBaseObject
{
	GENERATED_BODY()

public:
	UGizmoArrowObject();

	//~ Begin UGizmoBaseObject Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit LineTraceObject(const FVector RayOrigin, const FVector RayDirection) override;
	virtual void SetHoverState(bool bHoveringIn) override;
	virtual void SetInteractingState(bool bInteractingIn) override;
	virtual void SetWorldLocalState(bool bWorldIn) override;
	virtual void SetVisibility(bool bVisibleIn) override;
	virtual void SetLocalToWorldTransform(FTransform LocalToWorldTransformIn) override;
	virtual void SetGizmoScale(float InGizmoScale) override;
	virtual void SetMaterial(UMaterialInterface* InMaterial) override;
	virtual void SetCurrentMaterial(UMaterialInterface* InCurrentMaterial) override;
	//~ End UGizmoBaseObject Interface.

public:

	UPROPERTY(EditAnywhere, Category = Options)
	int32 bHasConeHead = true;
		
	UPROPERTY(EditAnywhere, Category = Options)
	FVector Direction = FVector(1.0f, 0.0f, 0.0f);

	// Arrow origin is located at (Direction * Offset) 
	UPROPERTY(EditAnywhere, Category = Options)
	float Offset = 0.0f;

	UPROPERTY()
	TObjectPtr<UGizmoCylinderObject> CylinderObject;

	UPROPERTY()
	TObjectPtr<UGizmoConeObject> ConeObject;

	UPROPERTY()
	TObjectPtr<UGizmoBoxObject> BoxObject;
};