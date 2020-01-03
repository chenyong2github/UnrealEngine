// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothConfig.h"
#include "ClothConfig_Legacy.h"
#include "ChaosClothSharedConfigCustomVersion.h"
#include "ClothingSimulationInteractor.h"

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
	EdgeStiffness = FMath::Clamp((VerticalStiffness + HorizontalStiffness) * 0.5f, 0.f, 1.f);

	BendingStiffness = FMath::Clamp(
		ClothConfig.BendConstraintConfig.Stiffness *
		ClothConfig.BendConstraintConfig.StiffnessMultiplier, 0.f, 1.f);

	AreaStiffness = FMath::Clamp(
		ClothConfig.ShearConstraintConfig.Stiffness *
		ClothConfig.ShearConstraintConfig.StiffnessMultiplier, 0.f, 1.f);

	AnimDriveSpringStiffness = FMath::Clamp(ClothConfig.AnimDriveSpringStiffness, 0.f, 1.f);

	CoefficientOfFriction = FMath::Clamp(ClothConfig.Friction, 0.f, 10.f);

	bUseBendingElements = false;
	bUseSelfCollisions = (ClothConfig.SelfCollisionRadius > 0.f && ClothConfig.SelfCollisionStiffness > 0.f);

	StrainLimitingStiffness = FMath::Clamp(ClothConfig.TetherStiffness, 0.f, 1.f);
	ShapeTargetStiffness = 0.f;
}

UChaosClothSharedSimConfig::UChaosClothSharedSimConfig()
{}

UChaosClothSharedSimConfig::~UChaosClothSharedSimConfig()
{}

void UChaosClothSharedSimConfig::MigrateFrom(const FClothConfig_Legacy& ClothConfig)
{
	IterationCount = FMath::Clamp(int32(ClothConfig.SolverFrequency / 60.f), 1, 100);

	SolverFrequency = ClothConfig.SolverFrequency;

	SelfCollisionThickness = FMath::Clamp(ClothConfig.SelfCollisionRadius, 0.f, 1000.f);

	CollisionThickness = FMath::Clamp(ClothConfig.CollisionThickness, 0.f, 1000.f);

	const float InDamping = (ClothConfig.Damping.X + ClothConfig.Damping.Y + ClothConfig.Damping.Z) / 3.f;
	Damping = FMath::Clamp(InDamping * InDamping * 0.95f, 0.f, 1.f);  // Nv Cloth seems to have a different damping formulation.

	bUseGravityOverride = ClothConfig.bUseGravityOverride;

	GravityScale = ClothConfig.GravityScale;

	Gravity = ClothConfig.GravityOverride;
}

void UChaosClothSharedSimConfig::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FChaosClothSharedConfigCustomVersion::GUID);
}

void UChaosClothSharedSimConfig::PostLoad()
{
	Super::PostLoad();
	const int32 ChaosClothSharedConfigCustomVersion = GetLinkerCustomVersion(FChaosClothSharedConfigCustomVersion::GUID);

	if (ChaosClothSharedConfigCustomVersion < FChaosClothSharedConfigCustomVersion::AddGravityOverride)
	{
		bUseGravityOverride = true;  // Default gravity override would otherwise disable the currently set gravity on older versions
	}
}

#if WITH_EDITOR
void UChaosClothSharedSimConfig::PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent)
{
	Super::PostEditChangeChainProperty(ChainEvent);

	// Update the simulation if there is any interactor attached to the skeletal mesh component
	if (ChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (USkeletalMesh* const OwnerMesh = Cast<USkeletalMesh>(GetOuter()))
		{
			for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
			{
				if (const USkeletalMeshComponent* const Component = *It)
				{
					if (Component->SkeletalMesh == OwnerMesh)
					{
						if (UClothingSimulationInteractor* const CurInteractor = Component->GetClothingSimulationInteractor())
						{
							CurInteractor->ClothConfigUpdated();
						}
					}
				}
			}
		}
	}
}
#endif  // #if WITH_EDITOR
