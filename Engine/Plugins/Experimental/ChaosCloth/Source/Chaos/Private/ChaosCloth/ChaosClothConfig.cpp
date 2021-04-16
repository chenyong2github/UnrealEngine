// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothConfig.h"
#include "ClothConfig_Legacy.h"
#include "ChaosClothConfigCustomVersion.h"
#include "ChaosClothSharedConfigCustomVersion.h"
#include "ClothingSimulationInteractor.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

// Legacy parameters not yet migrated to Chaos parameters:
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
//  LinearDrag
//  AngularDrag
//  StiffnessFrequency

UChaosClothConfig::UChaosClothConfig()
{}

UChaosClothConfig::~UChaosClothConfig()
{}

void UChaosClothConfig::MigrateFrom(const FClothConfig_Legacy& ClothConfig)
{
#if WITH_EDITORONLY_DATA
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

	AnimDriveStiffness.Low = 0.f;
	AnimDriveStiffness.High = FMath::Clamp(ClothConfig.AnimDriveSpringStiffness, 0.f, 1.f);

	AnimDriveDamping.Low = 0.f;
	AnimDriveDamping.High = FMath::Clamp(ClothConfig.AnimDriveDamperStiffness, 0.f, 1.f);

	FrictionCoefficient = FMath::Clamp(ClothConfig.Friction, 0.f, 10.f);

	bUseBendingElements = false;
	bUseSelfCollisions = (ClothConfig.SelfCollisionRadius > 0.f && ClothConfig.SelfCollisionStiffness > 0.f);

	TetherStiffness.Low = FMath::Clamp(ClothConfig.TetherStiffness, 0.f, 1.f);
	TetherStiffness.High = 1.f;
	LimitScale = FMath::Clamp(ClothConfig.TetherLimit, 0.01f, 10.f);
	ShapeTargetStiffness = 0.f;

	bUsePointBasedWindModel = (ClothConfig.WindMethod == EClothingWindMethod_Legacy::Legacy);
	DragCoefficient = bUsePointBasedWindModel ? 0.07f  : ClothConfig.WindDragCoefficient;  // Only Accurate wind uses the WindDragCoefficient
	LiftCoefficient = bUsePointBasedWindModel ? 0.035f : ClothConfig.WindLiftCoefficient;  // Only Accurate wind uses the WindLiftCoefficient

	const float Damping = (ClothConfig.Damping.X + ClothConfig.Damping.Y + ClothConfig.Damping.Z) / 3.f;
	DampingCoefficient = FMath::Clamp(Damping * Damping * 0.7f, 0.f, 1.f);  // Nv Cloth seems to have a different damping formulation.

	CollisionThickness = FMath::Clamp(ClothConfig.CollisionThickness, 0.f, 1000.f);
	SelfCollisionThickness = FMath::Clamp(ClothConfig.SelfCollisionRadius, 0.f, 1000.f);

	LinearVelocityScale = ClothConfig.LinearInertiaScale * 0.75f;
	const FVector AngularInertiaScale = ClothConfig.AngularInertiaScale * ClothConfig.CentrifugalInertiaScale * 0.75f;
	AngularVelocityScale = (AngularInertiaScale.X + AngularInertiaScale.Y + AngularInertiaScale.Z) / 3.f;

	bUseGravityOverride = ClothConfig.bUseGravityOverride;
	GravityScale = ClothConfig.GravityScale;
	Gravity = ClothConfig.GravityOverride;

	bUseLegacyBackstop = true;
#endif  // #if WITH_EDITORONLY_DATA
}

void UChaosClothConfig::MigrateFrom(const UClothSharedConfigCommon* ClothSharedConfig)
{
#if WITH_EDITORONLY_DATA
	if (const UChaosClothSharedSimConfig* const ChaosClothSharedSimConfig = Cast<UChaosClothSharedSimConfig>(ClothSharedConfig))
	{
		const int32 ChaosClothConfigCustomVersion = GetLinkerCustomVersion(FChaosClothConfigCustomVersion::GUID);

		if (ChaosClothConfigCustomVersion < FChaosClothConfigCustomVersion::AddDampingThicknessMigration)
		{
			if (ChaosClothSharedSimConfig->bUseDampingOverride_DEPRECATED)
			{
				DampingCoefficient = ChaosClothSharedSimConfig->Damping_DEPRECATED;
			}
			CollisionThickness = ChaosClothSharedSimConfig->CollisionThickness_DEPRECATED;
		}
		if (ChaosClothConfigCustomVersion < FChaosClothConfigCustomVersion::AddGravitySelfCollisionMigration)
		{
			SelfCollisionThickness = ChaosClothSharedSimConfig->SelfCollisionThickness_DEPRECATED;
			bUseGravityOverride = ChaosClothSharedSimConfig->bUseGravityOverride_DEPRECATED;
			GravityScale = ChaosClothSharedSimConfig->GravityScale_DEPRECATED;
			Gravity = ChaosClothSharedSimConfig->Gravity_DEPRECATED;
		}
	}
#endif  // #if WITH_EDITORONLY_DATA
}

void UChaosClothConfig::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FChaosClothConfigCustomVersion::GUID);
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
}

void UChaosClothConfig::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const int32 ChaosClothConfigCustomVersion = GetLinkerCustomVersion(FChaosClothConfigCustomVersion::GUID);
	const int32 PhysicsObjectVersion = GetLinkerCustomVersion(FPhysicsObjectVersion::GUID);
	const int32 FortniteMainBranchObjectVersion = GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	if (ChaosClothConfigCustomVersion < FChaosClothConfigCustomVersion::UpdateDragDefault)
	{
		DragCoefficient = 0.07f;  // Reset to a more appropriate default for chaos cloth assets saved before this custom version
	}

	if (ChaosClothConfigCustomVersion < FChaosClothConfigCustomVersion::RemoveInternalConfigParameters)
	{
		MinPerParticleMass = 0.0001f;  // Override these values in case they might have been accidentally
	}

	if (ChaosClothConfigCustomVersion < FChaosClothConfigCustomVersion::AddLegacyBackstopParameter)
	{
		bUseLegacyBackstop = true;
	}

	if (PhysicsObjectVersion < FPhysicsObjectVersion::ChaosClothAddWeightedValue)
	{
		AnimDriveStiffness.Low = 0.f;
		AnimDriveStiffness.High = FMath::Clamp(FMath::Loge(AnimDriveSpringStiffness_DEPRECATED) / FMath::Loge(1.e3f) + 1.f, 0.f, 1.f);
	}

	if (TetherMode_DEPRECATED != EChaosClothTetherMode::MaxChaosClothTetherMode)
	{
		// Note: MaxChaosClothTetherMode is used here to detect that the TetherMode parameter isn't set to its default value and therefore needs to be migrated.
		bUseGeodesicDistance = (TetherMode_DEPRECATED != EChaosClothTetherMode::FastTetherFastLength);
		TetherMode_DEPRECATED = EChaosClothTetherMode::MaxChaosClothTetherMode;
	}

	if (FortniteMainBranchObjectVersion < FFortniteMainBranchObjectVersion::ChaosClothAddfictitiousforces)
	{
		FictitiousAngularScale = 0.f;  // Maintain early behavior with no fictitious forces
	}

	if (PhysicsObjectVersion < FPhysicsObjectVersion::ChaosClothAddTetherStiffnessWeightMap)
	{
		// Note: Unlike AnimDriveStiffness, Low is updated here, because there was no existing weight map before this version
		TetherStiffness.Low = FMath::Clamp(FMath::Loge(StrainLimitingStiffness_DEPRECATED) / FMath::Loge(1.e3f) + 1.f, 0.f, 1.f);
		TetherStiffness.High = 0.f;
	}
#endif  // #if WITH_EDITORONLY_DATA
}

float UChaosClothConfig::GetMassValue() const
{
	switch (MassMode)
	{
	default:
	case EClothMassMode::Density: return Density;
	case EClothMassMode::TotalMass: return TotalMass;
	case EClothMassMode::UniformMass: return UniformMass;
	}
}

UChaosClothSharedSimConfig::UChaosClothSharedSimConfig()
{}

UChaosClothSharedSimConfig::~UChaosClothSharedSimConfig()
{}

void UChaosClothSharedSimConfig::MigrateFrom(const FClothConfig_Legacy& ClothConfig)
{
#if WITH_EDITORONLY_DATA
	IterationCount = FMath::Clamp(int32(ClothConfig.SolverFrequency / 60.f), 1, 100);

	bUseDampingOverride_DEPRECATED = false;  // Damping is migrated to per cloth configs
#endif
}

void UChaosClothSharedSimConfig::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FChaosClothSharedConfigCustomVersion::GUID);
}

void UChaosClothSharedSimConfig::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const int32 ChaosClothSharedConfigCustomVersion = GetLinkerCustomVersion(FChaosClothSharedConfigCustomVersion::GUID);

	if (ChaosClothSharedConfigCustomVersion < FChaosClothSharedConfigCustomVersion::AddGravityOverride)
	{
		bUseGravityOverride_DEPRECATED = true;  // Default gravity override would otherwise disable the currently set gravity on older versions
	}
#endif
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
