// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/GizmoBaseComponent.h"
#include "GizmoRectangleComponent.generated.h"

/**
 * Simple Component intended to be used as part of 3D Gizmos. 
 * Draws outline of 3D rectangle based on parameters.
 */
UCLASS(ClassGroup = Utility, HideCategories = (Physics, Collision, Lighting, Rendering, Mobile))
class INTERACTIVETOOLSFRAMEWORK_API UGizmoRectangleComponent : public UGizmoBaseComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	FVector DirectionX = FVector(0, 0, 1);

	UPROPERTY(EditAnywhere, Category = Options)
	FVector DirectionY = FVector(0, 1, 0);

	UPROPERTY(EditAnywhere, Category = Options)
	float OffsetX = 0.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	float OffsetY = 0.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	float LengthX = 20.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	float LengthY = 20.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	float Thickness = 2.0f;


private:
	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	// if true, we drew along -Direction instead of Direction, and so should hit-test accordingly
	bool bFlippedX;
	bool bFlippedY;

	// gizmo visibility
	bool bRenderVisibility = true;

};