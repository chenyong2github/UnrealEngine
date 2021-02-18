// Copyright Epic Games, Inc. All Rights Reserved.
#include "PhysicsEngine/BodyUtils.h"

#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#if WITH_CHAOS
#include "Chaos/MassProperties.h"
#include "Chaos/Utilities.h"
#include "Physics/Experimental/ChaosInterfaceUtils.h"
#endif

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#include "PhysicsEngine/PhysXSupport.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace BodyUtils
{
	inline float KgPerM3ToKgPerCm3(float KgPerM3)
	{
		//1m = 100cm => 1m^3 = (100cm)^3 = 1000000cm^3
		//kg/m^3 = kg/1000000cm^3
		const float M3ToCm3Inv = 1.f / (100.f * 100.f * 100.f);
		return KgPerM3 * M3ToCm3Inv;
	}

	inline float gPerCm3ToKgPerCm3(float gPerCm3)
	{
		//1000g = 1kg
		//kg/cm^3 = 1000g/cm^3 => g/cm^3 = kg/1000 cm^3
		const float gToKG = 1.f / 1000.f;
		return gPerCm3 * gToKG;
	}

	inline float GetBodyInstanceDensity(const FBodyInstance* OwningBodyInstance)
	{
		// physical material - nothing can weigh less than hydrogen (0.09 kg/m^3)
		float DensityKGPerCubicUU = 1.0f;
		if (UPhysicalMaterial* PhysMat = OwningBodyInstance->GetSimplePhysicalMaterial())
		{
			DensityKGPerCubicUU = FMath::Max(KgPerM3ToKgPerCm3(0.09f), gPerCm3ToKgPerCm3(PhysMat->Density));
		}
		return DensityKGPerCubicUU;
	}

#if WITH_CHAOS
	Chaos::FMassProperties ApplyMassPropertiesModifiers(const FBodyInstance* OwningBodyInstance, Chaos::FMassProperties MassProps, const FTransform& MassModifierTransform, const bool bInertaScaleIncludeMass)
	{
		float OldMass = MassProps.Mass;
		float NewMass = 0.f;

		if (OwningBodyInstance->bOverrideMass == false)
		{
			// The mass was calculated assuming uniform density. RaiseMassToPower for values of less than 1.0
			// is used to correct this for objects where the density is higher closer to the surface.
			float RaiseMassToPower = 0.75f;
			if (UPhysicalMaterial* PhysMat = OwningBodyInstance->GetSimplePhysicalMaterial())
			{
				RaiseMassToPower = PhysMat->RaiseMassToPower;
			}

			float UsePow = FMath::Clamp<float>(RaiseMassToPower, KINDA_SMALL_NUMBER, 1.f);
			NewMass = FMath::Pow(OldMass, UsePow);

			// Apply user-defined mass scaling.
			NewMass = FMath::Max(OwningBodyInstance->MassScale * NewMass, 0.001f);	//min weight of 1g
		}
		else
		{
			NewMass = FMath::Max(OwningBodyInstance->GetMassOverride(), 0.001f);	//min weight of 1g
		}

		float MassRatio = NewMass / OldMass;
		MassProps.Mass *= MassRatio;
		MassProps.InertiaTensor *= MassRatio;
		MassProps.CenterOfMass += MassModifierTransform.TransformVector(OwningBodyInstance->COMNudge);

		// Scale the inertia tensor by the owning body instance's InertiaTensorScale
		// NOTE: PhysX scales the inertia by the mass increase we would get from the scale change, even though we 
		// don't actually scale the mass at all based on InertiaScale. This is non-intuituve. E.g., you may expect 
		// that if InertiaScale = (S,S,S) and the mass is fixed (we already accounted for the effect of mass change on
		// ont the inertia just above), then the inertia components would roughly multiply by S^2, but actually
		// they end up multiplied by S^5. 
		// The option we choose is controlled by bInertaScaleIncludeMass.
		//		bInertaScaleIncludeMass = true: original behaviour as in PhysX
		//		bInertaScaleIncludeMass = false: more sensible behaviour given that InertiaScale does not affect mass
		if (!(OwningBodyInstance->InertiaTensorScale - FVector::OneVector).IsNearlyZero(1e-3f))
		{
			MassProps.InertiaTensor = Chaos::Utilities::ScaleInertia(MassProps.InertiaTensor, OwningBodyInstance->InertiaTensorScale, bInertaScaleIncludeMass);
		}

		return MassProps;
	}

	Chaos::FMassProperties ComputeMassProperties(const FBodyInstance* OwningBodyInstance, const TArray<FPhysicsShapeHandle>& Shapes, const FTransform& MassModifierTransform, const bool bInertaScaleIncludeMass)
	{
		// Calculate the mass properties based on the shapes assuming uniform density
		Chaos::FMassProperties MassProps;
		ChaosInterface::CalculateMassPropertiesFromShapeCollection(MassProps, Shapes, GetBodyInstanceDensity(OwningBodyInstance));

		// Apply the BodyInstance's mass and inertia modifiers
		return ApplyMassPropertiesModifiers(OwningBodyInstance, MassProps, MassModifierTransform, bInertaScaleIncludeMass);
	}

	Chaos::FMassProperties ComputeMassProperties(const FBodyInstance* OwningBodyInstance, const Chaos::FShapesArray& Shapes, const TArray<bool>& bContributesToMass, const FTransform& MassModifierTransform, const bool bInertaScaleIncludeMass)
	{
		// Calculate the mass properties based on the shapes assuming uniform density
		Chaos::FMassProperties MassProps;
		ChaosInterface::CalculateMassPropertiesFromShapeCollection(MassProps, Shapes, bContributesToMass, GetBodyInstanceDensity(OwningBodyInstance));

		// Apply the BodyInstance's mass and inertia modifiers
		return ApplyMassPropertiesModifiers(OwningBodyInstance, MassProps, MassModifierTransform, bInertaScaleIncludeMass);
	}


#elif PHYSICS_INTERFACE_PHYSX

	/** Computes and adds the mass properties (inertia, com, etc...) based on the mass settings of the body instance. */
	PxMassProperties ComputeMassProperties(const FBodyInstance* OwningBodyInstance, TArray<FPhysicsShapeHandle> Shapes, const FTransform& MassModifierTransform, const bool bUnused)
	{
		// physical material - nothing can weigh less than hydrogen (0.09 kg/m^3)
		float DensityKGPerCubicUU = 1.0f;
		float RaiseMassToPower = 0.75f;
		if (UPhysicalMaterial* PhysMat = OwningBodyInstance->GetSimplePhysicalMaterial())
		{
			DensityKGPerCubicUU = FMath::Max(KgPerM3ToKgPerCm3(0.09f), gPerCm3ToKgPerCm3(PhysMat->Density));
			RaiseMassToPower = PhysMat->RaiseMassToPower;
		}

		PxMassProperties MassProps;
		FPhysicsInterface::CalculateMassPropertiesFromShapeCollection(MassProps, Shapes, DensityKGPerCubicUU);

		float OldMass = MassProps.mass;
		float NewMass = 0.f;

		if (OwningBodyInstance->bOverrideMass == false)
		{
			float UsePow = FMath::Clamp<float>(RaiseMassToPower, KINDA_SMALL_NUMBER, 1.f);
			NewMass = FMath::Pow(OldMass, UsePow);

			// Apply user-defined mass scaling.
			NewMass = FMath::Max(OwningBodyInstance->MassScale * NewMass, 0.001f);	//min weight of 1g
		}
		else
		{
			NewMass = FMath::Max(OwningBodyInstance->GetMassOverride(), 0.001f);	//min weight of 1g
		}

		check(NewMass > 0.f);

		float MassRatio = NewMass / OldMass;

		PxMassProperties FinalMassProps = MassProps * MassRatio;

		FinalMassProps.centerOfMass += U2PVector(MassModifierTransform.TransformVector(OwningBodyInstance->COMNudge));
		FinalMassProps.inertiaTensor = PxMassProperties::scaleInertia(FinalMassProps.inertiaTensor, PxQuat(PxIdentity), U2PVector(OwningBodyInstance->InertiaTensorScale));

		return FinalMassProps;
	}
#endif

}