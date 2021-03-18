// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDSuspensionConstraints.h"

namespace Chaos
{
	FPBDSuspensionConstraintHandle::FPBDSuspensionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : 
		TContainerConstraintHandle<FPBDSuspensionConstraints>(StaticType(), InConstraintContainer, InConstraintIndex)
	{
	}

	const FPBDSuspensionSettings& FPBDSuspensionConstraintHandle::GetSettings() const
	{
		return ConstraintContainer->GetSettings(ConstraintIndex);
	}

	FPBDSuspensionSettings& FPBDSuspensionConstraintHandle::GetSettings()
	{
		return ConstraintContainer->GetSettings(ConstraintIndex);
	}

	void FPBDSuspensionConstraintHandle::SetSettings(const FPBDSuspensionSettings& Settings)
	{
		ConstraintContainer->SetSettings(ConstraintIndex, Settings);
	}


	TVec2<FGeometryParticleHandle*> FPBDSuspensionConstraintHandle::GetConstrainedParticles() const
	{
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex);
	}



	bool FPBDSuspensionConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const
	{
		for (FConstraintContainerHandle* ConstraintHandle : ConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}

		// #todo: Return true only if more iteration is needed
		return true;
	}

	Chaos::FPBDSuspensionConstraints::FConstraintContainerHandle* FPBDSuspensionConstraints::AddConstraint(TGeometryParticleHandle<FReal, 3>* Particle, const FVec3& InSuspensionLocalOffset, const FPBDSuspensionSettings& InConstraintSettings)
	{
		int32 NewIndex = ConstrainedParticles.Num();
		ConstrainedParticles.Add(Particle);
		SuspensionLocalOffset.Add(InSuspensionLocalOffset);
		ConstraintSettings.Add(InConstraintSettings);
		Handles.Add(HandleAllocator.AllocHandle(this, NewIndex));
		return Handles[NewIndex];
	}


	void FPBDSuspensionConstraints::RemoveConstraint(int ConstraintIndex)
	{
		FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		ConstrainedParticles.RemoveAtSwap(ConstraintIndex);
		SuspensionLocalOffset.RemoveAtSwap(ConstraintIndex);
		ConstraintSettings.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
		}
	}

	void FPBDSuspensionConstraints::ApplySingle(const FReal Dt, int32 ConstraintIndex) const
	{
		FGenericParticleHandle Particle = ConstrainedParticles[ConstraintIndex];
		const FPBDSuspensionSettings& Setting = ConstraintSettings[ConstraintIndex];

		if (Particle->IsDynamic() && Setting.Enabled)
		{
			const FVec3& T = Setting.Target;
			const FVec3 WorldSpaceX = Particle->Q().RotateVector(SuspensionLocalOffset[ConstraintIndex]) + Particle->P();

			FVec3 AxisWorld = Particle->Q() * Setting.Axis;
			FReal Distance = FVec3::DotProduct(WorldSpaceX - T, AxisWorld);
			if (Distance >= Setting.MaxLength)
			{
				// do nothing since the target point is further than the longest extension of the suspension spring
				return;
			}

			FVec3 DX = FVec3::ZeroVector;

			// Require the velocity at the WorldSpaceX position - not the velocity of the particle origin
			const FVec3 COM = Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(Particle);
			const FVec3 Diff = WorldSpaceX - COM;
			FVec3 ArmVelocity = Particle->V() - FVec3::CrossProduct(Diff, Particle->W());

			// This constraint is causing considerable harm to the steering effect from the tires, using only the z component for damping
			// makes this issue go away, rather than using DotProduct against the expected AxisWorld vector
			FReal PointVelocityAlongAxis = FVec3::DotProduct(ArmVelocity, AxisWorld);

			if (Distance < Setting.MinLength)
			{
				// target point distance is less at min compression limit 
				// - apply distance constraint to try keep a valid min limit
				FVec3 Ts = WorldSpaceX + AxisWorld * (Setting.MinLength - Distance);
				DX = (Ts - WorldSpaceX) * Setting.HardstopStiffness;

				Distance = Setting.MinLength;

				if (PointVelocityAlongAxis < 0.0f)
				{
					const FVec3 SpringVelocity = PointVelocityAlongAxis * AxisWorld;
					DX -= SpringVelocity * Setting.HardstopVelocityCompensation;
					PointVelocityAlongAxis = 0.0f; //this Dx will cancel velocity, so don't pass PointVelocityAlongAxis on to suspension force calculation 
				}
			}

			{
				// then the suspension force on top

				FReal DLambda = 0.f;
				{
					// #todo: Preload, better scaled spring damping like other suspension 0 -> 1 range
					FReal SpringCompression = (Setting.MaxLength - Distance) /*+ Setting.SpringPreload*/;

					FReal VelDt = PointVelocityAlongAxis;

					const bool bAccelerationMode = false;
					const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / (Particle->InvM()) : 1.0f;
					const FReal S = SpringMassScale * Setting.SpringStiffness * Dt * Dt;
					const FReal D = SpringMassScale * Setting.SpringDamping * Dt;
					DLambda = (S * SpringCompression - D * VelDt);
					DX += DLambda * AxisWorld;
				}
			}

			const FVec3 Arm = WorldSpaceX - Particle->P();

			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle);
			const FMatrix33 WorldSpaceInvI = Utilities::ComputeWorldSpaceInertia(Q0, Particle->InvI());

			FVec3 DP = Particle->InvM() * DX;
			FRotation3 DQ = FRotation3::FromElements(WorldSpaceInvI * FVec3::CrossProduct(Arm, DX), 0.f) * Q0 * FReal(0.5);

			P0 += DP;
			Q0 += DQ;
			Q0.Normalize();
			FParticleUtilities::SetCoMWorldTransform(Particle, P0, Q0);
		}

	}
	
	template class TContainerConstraintHandle<FPBDSuspensionConstraints>;


}
