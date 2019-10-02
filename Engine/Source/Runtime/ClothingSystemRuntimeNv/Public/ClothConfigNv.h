// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/SkeletalMesh.h"
#include "ClothConfig.h"
#include "ClothConfigNv.generated.h"

namespace nvidia
{
	namespace apex
	{
		class ClothingAsset;
	}
}

// Container for a constraint setup, these can be horizontal, vertical, shear and bend
USTRUCT()
struct FClothConstraintSetup
{
	GENERATED_BODY()

	FClothConstraintSetup()
		: Stiffness(1.0f)
		, StiffnessMultiplier(1.0f)
		, StretchLimit(1.0f)
		, CompressionLimit(1.0f)
	{}

	// How stiff this constraint is, this affects how closely it will follow the desired position
	UPROPERTY(EditAnywhere, Category=Constraint)
	float Stiffness;

	// A multiplier affecting the above value
	UPROPERTY(EditAnywhere, Category = Constraint)
	float StiffnessMultiplier;

	// The hard limit on how far this constarint can stretch
	UPROPERTY(EditAnywhere, Category = Constraint)
	float StretchLimit;

	// The hard limit on how far this constraint can compress
	UPROPERTY(EditAnywhere, Category = Constraint)
	float CompressionLimit;
};

UENUM()
enum class EClothingWindMethod : uint8
{
	// Use legacy wind mode, where accelerations are modified directly by the simulation
	// with no regard for drag or lift
	Legacy,

	// Use updated wind calculation for NvCloth based solved taking into account
	// drag and lift, this will require those properties to be correctly set in
	// the clothing configuration
	Accurate
};

/** Holds initial, asset level config for clothing actors. */
UCLASS()
class CLOTHINGSYSTEMRUNTIMENV_API UClothConfigNv : public UClothConfigBase
{
	GENERATED_BODY()
public:
	UClothConfigNv();

	bool HasSelfCollision() const override;

#if WITH_EDITOR
	virtual bool InitFromApexAssetCallback(
		nvidia::apex::ClothingAsset* InApexAsset,
		USkeletalMesh* TargetMesh,
		FName InName);
#endif // WITH_EDITOR

	// How wind should be processed, Accurate uses drag and lift to make the cloth react differently, legacy applies similar forces to all clothing without drag and lift (similar to APEX)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	EClothingWindMethod WindMethod;

	// Constraint data for vertical constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup VerticalConstraintConfig;

	// Constraint data for horizontal constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup HorizontalConstraintConfig;

	// Constraint data for bend constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup BendConstraintConfig;

	// Constraint data for shear constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup ShearConstraintConfig;

	// Size of self collision spheres centered on each vert
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float SelfCollisionRadius;

	// Stiffness of the spring force that will resolve self collisions
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float SelfCollisionStiffness;

	/** 
	 * Scale to use for the radius of the culling checks for self collisions.
	 * Any other self collision body within the radius of this check will be culled.
	 * This helps performance with higher resolution meshes by reducing the number
	 * of colliding bodies within the cloth. Reducing this will have a negative
	 * effect on performance!
	 */
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin="0", ClampMin="0"))
	float SelfCollisionCullScale;

	// Damping of particle motion per-axis
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector Damping;

	// Friction of the surface when colliding
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float Friction;

	// Drag coefficient for wind calculations, higher values mean wind has more lateral effect on cloth
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float WindDragCoefficient;

	// Lift coefficient for wind calculations, higher values make cloth rise easier in wind
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float WindLiftCoefficient;

	// Drag applied to linear particle movement per-axis
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector LinearDrag;

	// Drag applied to angular particle movement, higher values should limit material bending (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector AngularDrag;

	// Scale for linear particle inertia, how much movement should translate to linear motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	FVector LinearInertiaScale;

	// Scale for angular particle inertia, how much movement should translate to angular motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	FVector AngularInertiaScale;

	// Scale for centrifugal particle inertia, how much movement should translate to angular motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	FVector CentrifugalInertiaScale;

	// Frequency of the position solver, lower values will lead to stretchier, bouncier cloth
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "30", UIMax = "240", ClampMin = "30", ClampMax = "1000"))
	float SolverFrequency;

	// Frequency for stiffness calculations, lower values will degrade stiffness of constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float StiffnessFrequency;

	// Scale of gravity effect on particles
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (EditCondition = "!bUseGravityOverride"))
	float GravityScale;

	// Direct gravity override value
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (EditCondition = "bUseGravityOverride"))
	FVector GravityOverride;

	/** Use gravity override value vs gravity scale */
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (InlineEditConditionToggle))
	bool bUseGravityOverride;

	// Scale for stiffness of particle tethers between each other
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float TetherStiffness;

	// Scale for the limit of particle tethers (how far they can separate)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float TetherLimit;

	// 'Thickness' of the simulated cloth, used to adjust collisions
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float CollisionThickness;

	// Default spring stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float AnimDriveSpringStiffness;

	// Default damper stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float AnimDriveDamperStiffness;
};



/** Deprecated. Use UClothConfigNv instead. */
USTRUCT()
struct FClothConfig
{
	GENERATED_BODY()

	FClothConfig()
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

	void
	MigrateTo(UClothConfigNv* Config)
	{
		Config->WindMethod = WindMethod;
		Config->VerticalConstraintConfig = VerticalConstraintConfig;
		Config->HorizontalConstraintConfig = HorizontalConstraintConfig;
		Config->BendConstraintConfig = BendConstraintConfig;
		Config->ShearConstraintConfig = ShearConstraintConfig;
		Config->SelfCollisionRadius = SelfCollisionRadius;
		Config->SelfCollisionStiffness = SelfCollisionStiffness;
		Config->SelfCollisionCullScale = SelfCollisionCullScale;
		Config->Damping = Damping;
		Config->Friction = Friction;
		Config->WindDragCoefficient = WindDragCoefficient;
		Config->WindLiftCoefficient = WindLiftCoefficient;
		Config->LinearDrag = LinearDrag;
		Config->AngularDrag = AngularDrag;
		Config->LinearInertiaScale = LinearInertiaScale;
		Config->AngularInertiaScale = AngularInertiaScale;
		Config->CentrifugalInertiaScale = CentrifugalInertiaScale;
		Config->SolverFrequency = SolverFrequency;
		Config->StiffnessFrequency = StiffnessFrequency;
		Config->GravityScale = GravityScale;
		Config->GravityOverride = GravityOverride;
		Config->bUseGravityOverride = bUseGravityOverride;
		Config->TetherStiffness = TetherStiffness;
		Config->TetherLimit = TetherLimit;
		Config->CollisionThickness = CollisionThickness;
		Config->AnimDriveSpringStiffness = AnimDriveSpringStiffness;
		Config->AnimDriveDamperStiffness = AnimDriveDamperStiffness;
	}

	// How wind should be processed, Accurate uses drag and lift to make the cloth react differently, legacy applies similar forces to all clothing without drag and lift (similar to APEX)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	EClothingWindMethod WindMethod;

	// Constraint data for vertical constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup VerticalConstraintConfig;

	// Constraint data for horizontal constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup HorizontalConstraintConfig;

	// Constraint data for bend constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup BendConstraintConfig;

	// Constraint data for shear constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FClothConstraintSetup ShearConstraintConfig;

	// Size of self collision spheres centered on each vert
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float SelfCollisionRadius;

	// Stiffness of the spring force that will resolve self collisions
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float SelfCollisionStiffness;

	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin="0", ClampMin="0"))
	float SelfCollisionCullScale;

	// Damping of particle motion per-axis
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector Damping;

	// Friction of the surface when colliding
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float Friction;

	// Drag coefficient for wind calculations, higher values mean wind has more lateral effect on cloth
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float WindDragCoefficient;

	// Lift coefficient for wind calculations, higher values make cloth rise easier in wind
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float WindLiftCoefficient;

	// Drag applied to linear particle movement per-axis
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector LinearDrag;

	// Drag applied to angular particle movement, higher values should limit material bending (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	FVector AngularDrag;

	// Scale for linear particle inertia, how much movement should translate to linear motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	FVector LinearInertiaScale;

	// Scale for angular particle inertia, how much movement should translate to angular motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	FVector AngularInertiaScale;

	// Scale for centrifugal particle inertia, how much movement should translate to angular motion (per-axis)
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	FVector CentrifugalInertiaScale;

	// Frequency of the position solver, lower values will lead to stretchier, bouncier cloth
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (UIMin = "30", UIMax = "240", ClampMin = "30", ClampMax = "1000"))
	float SolverFrequency;

	// Frequency for stiffness calculations, lower values will degrade stiffness of constraints
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float StiffnessFrequency;

	// Scale of gravity effect on particles
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (EditCondition = "!bUseGravityOverride"))
	float GravityScale;

	// Direct gravity override value
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (EditCondition = "bUseGravityOverride"))
	FVector GravityOverride;

	// Use gravity override value vs gravity scale 
	UPROPERTY(EditAnywhere, Category = ClothConfig, meta = (InlineEditConditionToggle))
	bool bUseGravityOverride;

	// Scale for stiffness of particle tethers between each other
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float TetherStiffness;

	// Scale for the limit of particle tethers (how far they can separate)
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float TetherLimit;

	// 'Thickness' of the simulated cloth, used to adjust collisions
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float CollisionThickness;

	// Default spring stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float AnimDriveSpringStiffness;

	// Default damper stiffness for anim drive if an anim drive is in use
	UPROPERTY(EditAnywhere, Category = ClothConfig)
	float AnimDriveDamperStiffness;
};
