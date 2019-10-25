// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Engine/SkeletalMesh.h"
#include "ClothConfig.h"
#include "CoreMinimal.h"
#include "ChaosClothConfig.generated.h"

/** Holds initial, asset level config for clothing actors. */
// Hiding categories that will be used in the future
UCLASS(HideCategories = (Solver, Collision))
class CHAOSCLOTH_API UChaosClothConfig : public UClothConfigBase
{
	GENERATED_BODY()
public:
	UChaosClothConfig() {};

	/**
	 * How cloth particle mass is determined
	 * -	Uniform Mass: Every particle's mass will be set to the value specified in the UniformMass setting
	 * -	Total Mass: The total mass is distributed equally over all the particles
	 * -	Density: A constant mass density is used	 	 
	 */
	UPROPERTY(EditAnywhere, Category = MassConfig)
	EClothMassMode MassMode = EClothMassMode::Density;

	// The value used when the Mass Mode is set to Uniform Mass
	UPROPERTY(EditAnywhere, Category = MassConfig)
	float UniformMass = 1.0f;

	// The value used when Mass Mode is set to TotalMass
	UPROPERTY(EditAnywhere, Category = MassConfig)
	float TotalMass = 100.0f;

	/**
	 * The value used when Mass Mode is set to Density 
	 * Water: 1.0
	 * Cotton: 0.155
	 * Wool: 0.13
	 * Silk: 0.133
	 */
	UPROPERTY(EditAnywhere, Category = MassConfig)
	float Density = 0.1f;

	// This is a lower bound to cloth particle masses
	UPROPERTY(EditAnywhere, Category = MassConfig)
	float MinPerParticleMass = 0.0001f;

	// The Number of iterations used in the cloth solver
	// All cloth constraints will be more stiff when increased
	UPROPERTY(EditAnywhere, Category = Solver)
	int32 NumIterations = 1;

	// The Stiffness of the Edge constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float EdgeStiffness = 1.f;

	// The Stiffness of the bending constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float BendingStiffness = 1.f;

	// The stiffness of the area preservation constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AreaStiffness = 1.f;

	// The stiffness of the volume preservation constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float VolumeStiffness = 0.f;

	// The stiffness of the strain limiting constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float StrainLimitingStiffness = 1.f;
	
	// The stiffness of the shape target constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ShapeTargetStiffness = 0.f;

	// The radius of the self collisions spheres
	UPROPERTY(EditAnywhere, Category = Collision)
	float SelfCollisionThickness = 2.f;

	// The thickness of the cloth point spheres when colliding against collisions primitives
	UPROPERTY(EditAnywhere, Category = Collision)
	float CollisionThickness = 1.2;

	// Friction coefficient for cloth - collider interaction
	UPROPERTY(EditAnywhere, Category = Collision, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "10"))
	float CoefficientOfFriction = 0.0f;

	UPROPERTY(EditAnywhere, Category = Solver, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Damping = 0.01f;

	UPROPERTY(EditAnywhere, Category = Solver)
	float GravityMagnitude = 490.f;

	// Default spring stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AnimDriveSpringStiffness = 0.001f;

	// Enable bending elements
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseBendingElements = false;

	// Enable tetrahral constraints
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseTetrahedralConstraints = false;

	// Enable thin shell volume constraints 
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseThinShellVolumeConstraints = false;

	// Enable self collision
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseSelfCollisions = false;

	// Enable continuous collision detection
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseContinuousCollisionDetection = false;
};
