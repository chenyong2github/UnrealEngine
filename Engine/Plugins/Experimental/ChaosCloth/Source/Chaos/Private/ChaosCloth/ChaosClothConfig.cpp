// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothConfig.h"
#include "ClothConfig_Legacy.h"

// Legacy parameters not yet migrated to Chaos parameters:
//  WindDragCoefficient
//  WindMethod
//  VerticalConstraintConfig.CompressionLimit
//  VerticalConstraintConfig.StretchLimit
//  HorizontalConstraintConfig.CompressionLimit
//  HorizontalConstraintConfig.StretchLimit
//  BendConstraintConfig.CompressionLimit
//  BendConstraintConfig.StretchLimit
//  ShearConstraintConfig.CompressionLimit
//  ShearConstraintConfig.StretchLimit
//  SelfCollisionStiffness
//  SelfCollisionCullScale
//  WindLiftCoefficient
//  LinearDrag
//  AngularDrag
//  LinearInertiaScale
//  AngularInertiaScale
//  CentrifugalInertiaScale
//  SolverFrequency
//  StiffnessFrequency
//  TetherLimit
//  AnimDriveSpringStiffness
//  AnimDriveDamperStiffness

UChaosClothConfig::UChaosClothConfig()
{}

UChaosClothConfig::~UChaosClothConfig()
{}

void UChaosClothConfig::MigrateFrom(const FClothConfig_Legacy& ClothConfig)
{
	const float VerticalStiffness =
		ClothConfig.VerticalConstraintConfig.Stiffness *
		ClothConfig.VerticalConstraintConfig.StiffnessMultiplier;
	const float HorizontalStiffness =
		ClothConfig.HorizontalConstraintConfig.Stiffness *
		ClothConfig.HorizontalConstraintConfig.StiffnessMultiplier;
	EdgeStiffness = (VerticalStiffness + HorizontalStiffness) * 0.5f;

	BendingStiffness =
		ClothConfig.BendConstraintConfig.Stiffness *
		ClothConfig.BendConstraintConfig.StiffnessMultiplier;

	AreaStiffness =
		ClothConfig.ShearConstraintConfig.Stiffness *
		ClothConfig.ShearConstraintConfig.StiffnessMultiplier;

	AnimDriveSpringStiffness = ClothConfig.AnimDriveSpringStiffness;

	CoefficientOfFriction = FMath::Clamp(ClothConfig.Friction, 0.f, 10.f);

	bUseBendingElements = false;
	bUseSelfCollisions = (ClothConfig.SelfCollisionRadius > 0.f && ClothConfig.SelfCollisionStiffness > 0.f);

	StrainLimitingStiffness = ClothConfig.TetherStiffness;
	ShapeTargetStiffness = 0.f;
}

UChaosClothSharedSimConfig::UChaosClothSharedSimConfig()
	: Gravity(FVector(0.f, 0.f, -490.f))
{}

UChaosClothSharedSimConfig::~UChaosClothSharedSimConfig()
{}

void UChaosClothSharedSimConfig::MigrateFrom(const FClothConfig_Legacy& ClothConfig)
{
	IterationCount = FMath::Clamp(int32(ClothConfig.SolverFrequency / 60.f), 1, 100);

	SelfCollisionThickness = FMath::Clamp(ClothConfig.SelfCollisionRadius, 0.f, 1000.f);

	CollisionThickness = FMath::Clamp(ClothConfig.CollisionThickness, 0.f, 1000.f);

	const float InDamping = (ClothConfig.Damping.X + ClothConfig.Damping.Y + ClothConfig.Damping.Z) / 3.f;
	Damping = FMath::Clamp(InDamping * InDamping * 0.95f, 0.f, 1.f);  // Nv Cloth seems to have a different damping formulation.

	if (ClothConfig.bUseGravityOverride)
	{
		Gravity = ClothConfig.GravityOverride;
	}
	else
	{
		static const FVector WorldGravity(0.f, 0.f, -2880.f);  // TODO(Kriss.Gossart): Temporary Fortnite value. Use context gravity in Chaos Cloth Simulation instead.
		Gravity = WorldGravity * ClothConfig.GravityScale;
	}
}
