// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_WorldCollision.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"
#include "Components/PrimitiveComponent.h"

FRigUnit_SphereTraceWorld_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

    bHit = false;
	HitLocation = FVector::ZeroVector;
	HitNormal = FVector(0.f, 0.f, 1.f);

	if(Context.World == nullptr)
	{
		return;
	}
	
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;

	if (Context.OwningActor)
	{
		QueryParams.AddIgnoredActor(Context.OwningActor);
	}
	else if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Context.OwningComponent))
	{
		QueryParams.AddIgnoredComponent(PrimitiveComponent);
	}
	
	FCollisionResponseParams ResponseParams(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);

	const FCollisionShape CollisionShape = FCollisionShape::MakeSphere(Radius);

	FHitResult HitResult;
	bHit = Context.World->SweepSingleByChannel(HitResult, Context.ToWorldSpace(Start), Context.ToWorldSpace(End), 
			FQuat::Identity, Channel, CollisionShape, QueryParams, ResponseParams);

	if (bHit)
	{
		HitLocation = Context.ToRigSpace(HitResult.ImpactPoint);
		HitNormal = Context.ToWorldSpaceTransform.InverseTransformVector(HitResult.ImpactNormal);
	}
}
