// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/GizmoBaseComponent.h"
#include "GizmoCircleComponent.generated.h"

/**
 * Simple Component intended to be used as part of 3D Gizmos.
 * Draws a 3D circle based on parameters.
 */
UCLASS(ClassGroup = Utility, HideCategories = (Physics, Collision, Mobile))
class INTERACTIVETOOLSFRAMEWORK_API UGizmoCircleComponent : public UGizmoBaseComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	FVector Normal = FVector(0,0,1);

	UPROPERTY(EditAnywhere, Category = Options)
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	float Thickness = 2.0f;

	UPROPERTY(EditAnywhere, Category = Options)
	int NumSides = 64;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bViewAligned = false;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bOnlyAllowFrontFacingHits = true;

private:
	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params) override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.
};