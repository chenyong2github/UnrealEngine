// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ClothConfig.h"
#include "CoreMinimal.h"
#include "ChaosClothConfig.generated.h"

/** Holds initial, asset level config for clothing actors. */
// Hiding categories that will be used in the future
UCLASS(HideCategories = (Collision))
class CHAOSCLOTH_API UChaosClothConfig : public UClothConfigCommon
{
	GENERATED_BODY()
public:
	UChaosClothConfig();
	virtual ~UChaosClothConfig() override;

	/** Migrate from the legacy FClothConfig structure. */
	virtual void MigrateFrom(const FClothConfig_Legacy&) override;

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

	// The Stiffness of the Edge constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float EdgeStiffness = 0.5f;

	// The Stiffness of the bending constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float BendingStiffness = 0.5f;

	// The stiffness of the area preservation constraints
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AreaStiffness = 0.5f;

	// The stiffness of the volume preservation constraints
	UPROPERTY()
	float VolumeStiffness = 0.f;

	// The strain limiting stiffness of the long range attachment constraints (aka tether stiffness)
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float StrainLimitingStiffness = 0.5f;

	// The stiffness of the shape target constraints
	UPROPERTY()
	float ShapeTargetStiffness = 0.f;

	// Friction coefficient for cloth - collider interaction
	UPROPERTY(EditAnywhere, Category = Collision, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "10"))
	float CoefficientOfFriction = 0.f;

	// Default spring stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AnimDriveSpringStiffness = 0.001f;

	// Enable the more accurate bending element constraints instead of the faster cross-edge spring constraints used for controlling bending stiffness.
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseBendingElements = false;

	// Enable tetrahedral constraints
	UPROPERTY()
	bool bUseTetrahedralConstraints = false;

	// Enable thin shell volume constraints 
	UPROPERTY()
	bool bUseThinShellVolumeConstraints = false;

	// Enable self collision
	UPROPERTY(EditAnywhere, Category = ClothEnableFlags)
	bool bUseSelfCollisions = false;

	// Enable continuous collision detection
	UPROPERTY()
	bool bUseContinuousCollisionDetection = false;
};

/**
 * Chaos config settings shared between all instances of a skeletal mesh.
 * Unlike UChaosClothConfig, these settings contain common cloth simulation
 * parameters that cannot change between the various clothing assets assigned
 * to a specific skeletal mesh. @seealso UChaosClothConfig.
 */
UCLASS()
class CHAOSCLOTH_API UChaosClothSharedSimConfig : public UClothSharedConfigCommon
{
	GENERATED_BODY()
public:
	UChaosClothSharedSimConfig();
	virtual ~UChaosClothSharedSimConfig() override;

	/** Migrate from the legacy FClothConfig structure. */
	virtual void MigrateFrom(const FClothConfig_Legacy&) override;

	/** Serialize override used to set the current custom version. */
	virtual void Serialize(FArchive& Ar) override;

	/** PostLoad override used to deal with updates/changes in properties. */
	virtual void PostLoad() override;

#if WITH_EDITOR
	/** Called after changes in any of the asset properties. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent) override;
#endif

	// The number of solver iterations
	// This will increase the stiffness of all constraints but will increase the CPU cost
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "0", UIMax = "20", ClampMin = "0", ClampMax = "100"))
	int32 IterationCount = 1;

	// The expected simulation frequency in Hz that, if not closely matching the game tick frequency, will trigger substepping to help keep a more consistent time step.
	// Use higher values to avoid jitter, and smaller value to avoid the extra cost of substepping.
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "30", UIMax = "240", ClampMin = "1", ClampMax = "1080"))
	float SolverFrequency = 60.f;

	// The radius of the spheres used in self collision 
	UPROPERTY(EditAnywhere, Category = Collision, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	float SelfCollisionThickness = 2.0f;

	// The radius of cloth points when considering collisions against collider shapes
	UPROPERTY(EditAnywhere, Category = Collision, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	float CollisionThickness = 1.0f;

	//The amount of cloth damping
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Damping = 0.01f;

	// Use gravity value vs world gravity
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (InlineEditConditionToggle))
	bool bUseGravityOverride = false;

	// Scale factor applied to the world gravity when not using the gravity override
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (EditCondition = "!bUseGravityOverride"))
	float GravityScale = 1.f;

	// The gravitational acceleration vector [cm/s^2]
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (EditCondition = "bUseGravityOverride"))
	FVector Gravity = { 0.f, 0.f, -980.665f };

	// The wind drag coefficient applying on each particle
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "10"))
	float WindDrag = 0.5f;

	// Enable the XPBD constraints that resolve stiffness independently from the number of iterations
	// Experimental, this feature might be removed without warning, not for production use
	UPROPERTY(EditAnywhere, Category = "Experimental", meta = (DisplayName="Use XPBD Constraints (Experimental)"))
	bool bUseXPBDConstraints = false;
};
