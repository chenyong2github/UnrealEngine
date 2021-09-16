// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "InputState.h"
#include "GizmoGroupObject.generated.h"

/**
 * Simple group object intended to be used as part of 3D Gizmos.
 * Contains multiple gizmo objects.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoGroupObject : public UGizmoBaseObject
{
	GENERATED_BODY()

public:
	UGizmoGroupObject();

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

	virtual void Add(UGizmoBaseObject* Object)
	{
		Objects.Add(Object);
	}

public:

	UPROPERTY(EditAnywhere, Category = Options)
	TArray<TObjectPtr<UGizmoBaseObject>> Objects;
};