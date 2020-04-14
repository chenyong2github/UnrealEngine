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

	/** Migrate from shared config. */
	virtual void MigrateFrom(const UClothSharedConfigCommon* ClothSharedConfig) override;

	/** Serialize override used to set the current custom version. */
	virtual void Serialize(FArchive& Ar) override;

	/** PostLoad override used to deal with updates/changes in properties. */
	virtual void PostLoad() override;

	/**
	 * How cloth particle mass is determined
	 * -	Uniform Mass: Every particle's mass will be set to the value specified in the UniformMass setting. Mostly to avoid as it can cause some serious issues with irregular tessellations.
	 * -	Total Mass: The total mass is distributed equally over all the particles. Useful when referencing a specific garment size and feel.
	 * -	Density: A constant mass density is used. Density is usually the preferred way of setting mass since it allows matching real life materials' density values.
	 */
	UPROPERTY(EditAnywhere, Category = "Mass Config")
	EClothMassMode MassMode = EClothMassMode::Density;

	// The value used when the Mass Mode is set to Uniform Mass
	UPROPERTY(EditAnywhere, Category = "Mass Config", meta = (UIMin = "0.000001", UIMax = "0.001", ClampMin = "0"))
	float UniformMass = 0.00015f;

	// The value used when Mass Mode is set to TotalMass
	UPROPERTY(EditAnywhere, Category = "Mass Config", meta = (UIMin = "0.001", UIMax = "10", ClampMin = "0"))
	float TotalMass = 0.5f;

	/**
	 * The value used when Mass Mode is set to Density.
	 * Melton Wool: 0.7
	 * Heavy leather: 0.6
	 * Polyurethane: 0.5
	 * Denim: 0.4
	 * Light leather: 0.3
	 * Cotton: 0.2
	 * Silk: 0.1
	 */
	UPROPERTY(EditAnywhere, Category = "Mass Config", meta = (UIMin = "0.001", UIMax = "1", ClampMin = "0"))
	float Density = 0.35f;

	// This is a lower bound to cloth particle masses.
	UPROPERTY(EditAnywhere, Category = "Mass Config")
	float MinPerParticleMass = 0.0001f;	

	// The Stiffness of the Edge constraints, only use lower than 1 values for very stretchy materials. Increase the iteration count for stiffer materials.
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float EdgeStiffness = 1.f;

	// The Stiffness of the bending constraints. Increase the iteration count for stiffer materials.
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float BendingStiffness = 1.f;

	// The stiffness of the area preservation constraints. Increase the iteration count for stiffer materials.
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AreaStiffness = 1.f;

	// The stiffness of the volume preservation constraints.
	UPROPERTY()
	float VolumeStiffness = 0.f;

	// The strain limiting stiffness of the long range attachment constraints (aka tether stiffness).
	// The long range attachment connects each of the cloth particles to its closest fixed point with a spring constraint.
	// This can be used to compensate for a lack of stretch resistance when the iterations count is kept low for performance reasons.
	// Can lead to an unnatural pull string puppet like behavior. Use 0 to disable. 
	UPROPERTY(EditAnywhere, Category = "Long Range Attachment", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float StrainLimitingStiffness = 0.5f;

	// The limit scale of the long range attachment constraints (aka tether limit).
	UPROPERTY(EditAnywhere, Category = "Long Range Attachment", meta = (UIMin = "1.", UIMax = "1.1", ClampMin = "0.01", ClampMax = "10"))
	float LimitScale = 1.f;

	// Use geodesic instead of euclidean distance calculations in the long range attachment constraint,
	// which is slower at setup but less prone to artifacts during simulation.
	UPROPERTY(EditAnywhere, Category = "Long Range Attachment")
	bool bUseGeodesicDistance = true;

	// The stiffness of the shape target constraints
	UPROPERTY()
	float ShapeTargetStiffness = 0.f;

	// The radius of cloth points when considering collisions against collider shapes.
	UPROPERTY(EditAnywhere, Category = "Cloth Dynamics", meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	float CollisionThickness = 1.0f;

	// Friction coefficient for cloth - collider interaction.
	UPROPERTY(EditAnywhere, Category = "Cloth Dynamics", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "10"))
	float FrictionCoefficient = 0.2f;

	// The amount of damping applied to the cloth velocities.
	UPROPERTY(EditAnywhere, Category = "Cloth Dynamics", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float DampingCoefficient = 0.01f;

	// The drag coefficient applying on each particle.
	UPROPERTY(EditAnywhere, Category = "Cloth Dynamics", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "10"))
	float DragCoefficient = 0.1f;

	// Default spring stiffness for anim drive if an anim drive is in use.
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AnimDriveSpringStiffness = 0.001f;

	// Enable the more accurate bending element constraints instead of the faster cross-edge spring constraints used for controlling bending stiffness.
	UPROPERTY(EditAnywhere, Category = "Cloth Enable Flags")
	bool bUseBendingElements = false;

	// Enable tetrahedral constraints.
	UPROPERTY()
	bool bUseTetrahedralConstraints = false;

	// Enable thin shell volume constraints.
	UPROPERTY()
	bool bUseThinShellVolumeConstraints = false;

	// Enable self collision.
	UPROPERTY(EditAnywhere, Category = "Cloth Enable Flags")
	bool bUseSelfCollisions = false;

	// Enable continuous collision detection.
	UPROPERTY()
	bool bUseContinuousCollisionDetection = false;

	// The amount of linear velocities sent to the local cloth space from the reference bone
	// (the closest bone to the root on which the cloth section has been skinned, or the root itself if the cloth isn't skinned).
	UPROPERTY(EditAnywhere, Category = "Velocity Scales", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	FVector LinearVelocityScale = { 0.75f, 0.75f, 0.75f };

	// The amount of angular velocities sent to the local cloth space from the reference bone
	// (the closest bone to the root on which the cloth section has been skinned, or the root itself if the cloth isn't skinned).
	UPROPERTY(EditAnywhere, Category = "Velocity Scales", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float AngularVelocityScale = 0.75f;
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
	virtual void MigrateFrom(const FClothConfig_Legacy& ClothConfig) override;

	/** Serialize override used to set the current custom version. */
	virtual void Serialize(FArchive& Ar) override;

	/** PostLoad override used to deal with updates/changes in properties. */
	virtual void PostLoad() override;

#if WITH_EDITOR
	/** Called after changes in any of the asset properties. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent) override;
#endif

	// The number of solver iterations.
	// This will increase the stiffness of all constraints but will increase the CPU cost.
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "1", UIMax = "10", ClampMin = "0", ClampMax = "100"))
	int32 IterationCount = 2;

	// The number of solver substeps.
	// This will increase the precision of the collision inputs and help with constraint resolutions but will increase the CPU cost.
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (UIMin = "1", UIMax = "10", ClampMin = "1", ClampMax = "100"))
	int32 SubdivisionCount = 1;

	// The radius of the spheres used in self collision 
	UPROPERTY(EditAnywhere, Category = Collision, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	float SelfCollisionThickness = 2.0f;

	// The radius of cloth points when considering collisions against collider shapes.
	UPROPERTY()
	float CollisionThickness_DEPRECATED = 1.0f;

	// Use shared config damping rather than per cloth damping.
	UPROPERTY()
	bool bUseDampingOverride_DEPRECATED = true;

	// The amount of cloth damping. Override the per cloth damping coefficients.
	UPROPERTY()
	float Damping_DEPRECATED = 0.01f;

	// Use the config gravity value instead of world gravity.
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (InlineEditConditionToggle))
	bool bUseGravityOverride = false;

	// Scale factor applied to the world gravity and also to the clothing simulation interactor gravity. Does not affect the gravity if set using the override below.
	UPROPERTY(EditAnywhere, Category = Simulation)
	float GravityScale = 1.f;

	// The gravitational acceleration vector [cm/s^2]
	UPROPERTY(EditAnywhere, Category = Simulation, meta = (EditCondition = "bUseGravityOverride"))
	FVector Gravity = { 0.f, 0.f, -980.665f };

	// Enable local space simulation to help with floating point precision errors if the character is far away form the world origin
	UPROPERTY(EditAnywhere, Category = Simulation)
	bool bUseLocalSpaceSimulation = true;

	// Enable the XPBD constraints that resolve stiffness independently from the number of iterations
	// Experimental, this feature might be removed without warning, not for production use
	UPROPERTY()
	bool bUseXPBDConstraints = false;
};
