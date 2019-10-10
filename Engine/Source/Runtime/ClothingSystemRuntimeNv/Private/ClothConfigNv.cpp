// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothConfigNv.h"

UClothConfigNv::UClothConfigNv()
	: WindMethod(EClothingWindMethod::Legacy)
	, SelfCollisionRadius(0.0f)
	, SelfCollisionStiffness(0.0f)
	, SelfCollisionCullScale(1.0f)
	, Damping(0.4f)
	, Friction(0.1f)
	, WindDragCoefficient(0.02f/100.0f)
	, WindLiftCoefficient(0.02f/100.0f)
	, LinearDrag(0.2f)
	, AngularDrag(0.2f)
	, LinearInertiaScale(1.0f)
	, AngularInertiaScale(1.0f)
	, CentrifugalInertiaScale(1.0f)
	, SolverFrequency(120.0f)
	, StiffnessFrequency(100.0f)
	, GravityScale(1.0f)
	, GravityOverride(FVector::ZeroVector)
	, bUseGravityOverride(false)
	, TetherStiffness(1.0f)
	, TetherLimit(1.0f)
	, CollisionThickness(1.0f)
	, AnimDriveSpringStiffness(1.0f)
	, AnimDriveDamperStiffness(1.0f)
{}

bool UClothConfigNv::HasSelfCollision() const
{
	return SelfCollisionRadius > 0.0f && SelfCollisionStiffness > 0.0f;
}

#if WITH_EDITOR
bool UClothConfigNv::InitFromApexAssetCallback(
	nvidia::apex::ClothingAsset* InApexAsset,
	USkeletalMesh* TargetMesh,
	FName InName) 
{
#if WITH_APEX_CLOTHING
	// Set to use legacy wind calculations, which is what APEX would normally have used
	WindMethod = EClothingWindMethod::Legacy;
#endif // WITH_APEX_CLOTHING
	return false;
}

#endif //WITH_EDITOR
