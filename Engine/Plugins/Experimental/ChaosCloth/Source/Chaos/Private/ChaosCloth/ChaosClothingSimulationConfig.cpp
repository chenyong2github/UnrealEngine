// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Chaos/PBDLongRangeConstraints.h"  // For Tether modes
#include "GeometryCollection/ManagedArrayCollection.h"

namespace Chaos
{
	FClothingSimulationConfig::FClothingSimulationConfig()
		: PropertyCollection(MakeShared<FManagedArrayCollection>())
		, Properties(MakeUnique<Softs::FCollectionPropertyMutableFacade>(PropertyCollection))
	{
	}

	FClothingSimulationConfig::FClothingSimulationConfig(const TSharedPtr<const FManagedArrayCollection>& InPropertyCollection)
		: PropertyCollection(MakeShared<FManagedArrayCollection>())
		, Properties(MakeUnique<Softs::FCollectionPropertyMutableFacade>(PropertyCollection))
	{
		Initialize(InPropertyCollection);
	}

	FClothingSimulationConfig::~FClothingSimulationConfig() = default;

	void FClothingSimulationConfig::Initialize(const UChaosClothConfig* ClothConfig, const UChaosClothSharedSimConfig* ClothSharedConfig, bool bUseLegacyConfig)
	{
		constexpr ::Chaos::Softs::ECollectionPropertyFlags NonAnimatablePropertyFlags =
			::Chaos::Softs::ECollectionPropertyFlags::Enabled |
			::Chaos::Softs::ECollectionPropertyFlags::Legacy;  // Indicates a property set from a pre-property collection config (e.g. that can be overriden in Dataflow without warning)
		constexpr ::Chaos::Softs::ECollectionPropertyFlags AnimatablePropertyFlags = NonAnimatablePropertyFlags |
			::Chaos::Softs::ECollectionPropertyFlags::Animatable;

		// Clear all properties
		PropertyCollection->Reset();
		Properties->DefineSchema();

		// Solver properties
		if (ClothSharedConfig)
		{
			Properties->AddValue(TEXT("NumIterations"), ClothSharedConfig->IterationCount, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("MaxNumIterations"), ClothSharedConfig->MaxIterationCount, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("NumSubsteps"), ClothSharedConfig->SubdivisionCount, AnimatablePropertyFlags);
		}

		// Cloth properties
		if (ClothConfig)
		{
			// Mass
			{
				float MassValue;
				switch (ClothConfig->MassMode)
				{
				case EClothMassMode::TotalMass:
					MassValue = ClothConfig->TotalMass;
					break;
				case EClothMassMode::UniformMass:
					MassValue = ClothConfig->UniformMass;
					break;
				default:
				case EClothMassMode::Density:
					MassValue = ClothConfig->Density;
					break;
				}
				Properties->AddValue(TEXT("MassMode"), (int32)ClothConfig->MassMode, NonAnimatablePropertyFlags);
				Properties->AddValue(TEXT("MassValue"), MassValue, NonAnimatablePropertyFlags);
				Properties->AddValue(TEXT("MinPerParticleMass"), ClothConfig->MinPerParticleMass, NonAnimatablePropertyFlags);
			}

			// Edge constraint
			if (ClothConfig->EdgeStiffnessWeighted.Low > 0.f || ClothConfig->EdgeStiffnessWeighted.High > 0.f)
			{
				const int32 EdgeSpringStiffnessIndex = Properties->AddProperty(TEXT("EdgeSpringStiffness"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(EdgeSpringStiffnessIndex, ClothConfig->EdgeStiffnessWeighted.Low, ClothConfig->EdgeStiffnessWeighted.High);
				Properties->SetStringValue(EdgeSpringStiffnessIndex, TEXT("EdgeStiffness"));
			}

			// Bending constraint
			if (ClothConfig->BendingStiffnessWeighted.Low > 0.f || ClothConfig->BendingStiffnessWeighted.High > 0.f ||
				(ClothConfig->bUseBendingElements && (ClothConfig->BucklingStiffnessWeighted.Low > 0.f || ClothConfig->BucklingStiffnessWeighted.High > 0.f)))
			{
				if (ClothConfig->bUseBendingElements)
				{
					const int32 BendingElementStiffnessIndex = Properties->AddProperty(TEXT("BendingElementStiffness"), AnimatablePropertyFlags);
					Properties->SetWeightedValue(BendingElementStiffnessIndex, ClothConfig->BendingStiffnessWeighted.Low, ClothConfig->BendingStiffnessWeighted.High);
					Properties->SetStringValue(BendingElementStiffnessIndex, TEXT("BendingStiffness"));

					Properties->AddValue(TEXT("BucklingRatio"), ClothConfig->BucklingRatio, NonAnimatablePropertyFlags);

					if (ClothConfig->BucklingStiffnessWeighted.Low > 0.f || ClothConfig->BucklingStiffnessWeighted.High > 0.f)
					{
						const int32 BucklingStiffnessIndex = Properties->AddProperty(TEXT("BucklingStiffness"), AnimatablePropertyFlags);
						Properties->SetWeightedValue(BucklingStiffnessIndex, ClothConfig->BucklingStiffnessWeighted.Low, ClothConfig->BucklingStiffnessWeighted.High);
						Properties->SetStringValue(BucklingStiffnessIndex, TEXT("BucklingStiffness"));
					}
				}
				else  // Not using bending elements
				{
					const int32 BendingSpringStiffnessIndex = Properties->AddProperty(TEXT("BendingSpringStiffness"), AnimatablePropertyFlags);
					Properties->SetWeightedValue(BendingSpringStiffnessIndex, ClothConfig->BendingStiffnessWeighted.Low, ClothConfig->BendingStiffnessWeighted.High);
					Properties->SetStringValue(BendingSpringStiffnessIndex, TEXT("BendingStiffness"));
				}
			}

			// Area constraint
			if (ClothConfig->AreaStiffnessWeighted.Low > 0.f || ClothConfig->AreaStiffnessWeighted.High > 0.f)
			{
				const int32 AreaSpringStiffnessIndex = Properties->AddProperty(TEXT("AreaSpringStiffness"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(AreaSpringStiffnessIndex, ClothConfig->AreaStiffnessWeighted.Low, ClothConfig->AreaStiffnessWeighted.High);
				Properties->SetStringValue(AreaSpringStiffnessIndex, TEXT("AreaStiffness"));
			}

			// Long range attachment
			if (ClothConfig->TetherStiffness.Low > 0.f || ClothConfig->TetherStiffness.High > 0.f)
			{
				Properties->AddValue(TEXT("UseGeodesicTethers"), ClothConfig->bUseGeodesicDistance, NonAnimatablePropertyFlags);

				const int32 TetherStiffnessIndex = Properties->AddProperty(TEXT("TetherStiffness"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(TetherStiffnessIndex, ClothConfig->TetherStiffness.Low, ClothConfig->TetherStiffness.High);
				Properties->SetStringValue(TetherStiffnessIndex, TEXT("TetherStiffness"));

				const int32 TetherScaleIndex = Properties->AddProperty(TEXT("TetherScale"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(TetherScaleIndex, ClothConfig->TetherScale.Low, ClothConfig->TetherScale.High);
				Properties->SetStringValue(TetherScaleIndex, TEXT("TetherScale"));
			}

			// AnimDrive
			if (ClothConfig->AnimDriveStiffness.Low > 0.f || ClothConfig->AnimDriveStiffness.High > 0.f)
			{
				const int32 AnimDriveStiffnessIndex = Properties->AddProperty(TEXT("AnimDriveStiffness"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(AnimDriveStiffnessIndex, ClothConfig->AnimDriveStiffness.Low, ClothConfig->AnimDriveStiffness.High);
				Properties->SetStringValue(AnimDriveStiffnessIndex, TEXT("AnimDriveStiffness"));

				const int32 AnimDriveDampingIndex = Properties->AddProperty(TEXT("AnimDriveDamping"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(AnimDriveDampingIndex, ClothConfig->AnimDriveDamping.Low, ClothConfig->AnimDriveDamping.High);
				Properties->SetStringValue(AnimDriveDampingIndex, TEXT("AnimDriveDamping"));
			}

			// Gravity
			{
				Properties->AddValue(TEXT("GravityScale"), ClothConfig->GravityScale, AnimatablePropertyFlags);
				Properties->AddValue(TEXT("UseGravityOverride"), ClothConfig->bUseGravityOverride, AnimatablePropertyFlags);
				Properties->AddValue(TEXT("GravityOverride"), FVector3f(ClothConfig->Gravity), AnimatablePropertyFlags);
			}

			// Velocity scale
			{
				Properties->AddValue(TEXT("LinearVelocityScale"), FVector3f(ClothConfig->LinearVelocityScale), AnimatablePropertyFlags);
				Properties->AddValue(TEXT("AngularVelocityScale"), ClothConfig->AngularVelocityScale, AnimatablePropertyFlags);
				Properties->AddValue(TEXT("FictitiousAngularScale"), ClothConfig->FictitiousAngularScale, AnimatablePropertyFlags);
			}

			// Aerodynamics
			Properties->AddValue(TEXT("UsePointBasedWindModel"), ClothConfig->bUsePointBasedWindModel, NonAnimatablePropertyFlags);
			if (!ClothConfig->bUsePointBasedWindModel && (ClothConfig->Drag.Low > 0.f || ClothConfig->Drag.High > 0.f || ClothConfig->Lift.Low > 0.f || ClothConfig->Lift.High > 0.f))
			{
				const int32 DragIndex = Properties->AddProperty(TEXT("Drag"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(DragIndex, ClothConfig->Drag.Low, ClothConfig->Drag.High);
				Properties->SetStringValue(DragIndex, TEXT("Drag"));

				const int32 LiftIndex = Properties->AddProperty(TEXT("Lift"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(LiftIndex, ClothConfig->Lift.Low, ClothConfig->Lift.High);
				Properties->SetStringValue(LiftIndex, TEXT("Lift"));

				constexpr float AirDensity = 1.225f;
				Properties->AddValue(TEXT("FluidDensity"), AirDensity, AnimatablePropertyFlags);
			}

			// Pressure
			if (ClothConfig->Pressure.Low != 0.f || ClothConfig->Pressure.High != 0.f)
			{
				const int32 PressureIndex = Properties->AddProperty(TEXT("Pressure"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(PressureIndex, ClothConfig->Pressure.Low, ClothConfig->Pressure.High);
				Properties->SetStringValue(PressureIndex, TEXT("Pressure"));
			}

			// Damping
			Properties->AddValue(TEXT("DampingCoefficient"), ClothConfig->DampingCoefficient, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("LocalDampingCoefficient"), ClothConfig->LocalDampingCoefficient, AnimatablePropertyFlags);

			// Collision
			Properties->AddValue(TEXT("CollisionThickness"), ClothConfig->CollisionThickness, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("FrictionCoefficient"), ClothConfig->FrictionCoefficient, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("UseCCD"), ClothConfig->bUseCCD, AnimatablePropertyFlags);
			Properties->AddValue(TEXT("UseSelfCollisions"), ClothConfig->bUseSelfCollisions, NonAnimatablePropertyFlags);
			Properties->AddValue(TEXT("SelfCollisionThickness"), ClothConfig->SelfCollisionThickness, NonAnimatablePropertyFlags);
			Properties->AddValue(TEXT("UseSelfIntersections"), ClothConfig->bUseSelfIntersections, NonAnimatablePropertyFlags);
			Properties->AddValue(TEXT("SelfCollisionFriction"), ClothConfig->SelfCollisionFriction, NonAnimatablePropertyFlags);

			// Max distance
			{
				const int32 MaxDistanceIndex = Properties->AddProperty(TEXT("MaxDistance"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(MaxDistanceIndex, 0.f, 1.f);  // Backward compatibility with legacy mask must use a unit range since the multiplier is in the mask
				Properties->SetStringValue(MaxDistanceIndex, TEXT("MaxDistance"));
			}

			// Backstop
			{
				const int32 BackstopDistanceIndex = Properties->AddProperty(TEXT("BackstopDistance"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(BackstopDistanceIndex, 0.f, 1.f);  // Backward compatibility with legacy mask must use a unit range since the multiplier is in the mask
				Properties->SetStringValue(BackstopDistanceIndex, TEXT("BackstopDistance"));

				const int32 BackstopRadiusIndex = Properties->AddProperty(TEXT("BackstopRadius"), AnimatablePropertyFlags);
				Properties->SetWeightedValue(BackstopRadiusIndex, 0.f, 1.f);  // Backward compatibility with legacy mask must use a unit range since the multiplier is in the mask
				Properties->SetStringValue(BackstopRadiusIndex, TEXT("BackstopRadius"));

				Properties->AddValue(TEXT("UseLegacyBackstop"), ClothConfig->bUseLegacyBackstop, NonAnimatablePropertyFlags);
			}
		}

		// Mark this as a potential legacy config, but leave the behavior control to the client code (usually means constraint are removed with 0 stiffness, or missing weight maps)
		Properties->AddValue(TEXT("UseLegacyConfig"), bUseLegacyConfig, NonAnimatablePropertyFlags);
	}

	void FClothingSimulationConfig::Initialize(const TSharedPtr<const FManagedArrayCollection>& InPropertyCollection)
	{
		Properties->Copy(*InPropertyCollection);
	}

	const Softs::FCollectionPropertyConstFacade& FClothingSimulationConfig::GetProperties() const
	{
		return *Properties;
	}

	Softs::FCollectionPropertyFacade& FClothingSimulationConfig::GetProperties()
	{
		return *Properties;
	}
}  // End namespace Chaos
