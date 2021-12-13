// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/Evolution/SolverDatas.h"

namespace Chaos
{
	FPBDSuspensionConstraintHandle::FPBDSuspensionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : 
		TIndexedContainerConstraintHandle<FPBDSuspensionConstraints>(InConstraintContainer, InConstraintIndex)
	{
	}

	const FPBDSuspensionSettings& FPBDSuspensionConstraintHandle::GetSettings() const
	{
		return ConcreteContainer()->GetSettings(ConstraintIndex);
	}

	FPBDSuspensionSettings& FPBDSuspensionConstraintHandle::GetSettings()
	{
		return ConcreteContainer()->GetSettings(ConstraintIndex);
	}

	void FPBDSuspensionConstraintHandle::SetSettings(const FPBDSuspensionSettings& Settings)
	{
		ConcreteContainer()->SetSettings(ConstraintIndex, Settings);
	}

	TVec2<FGeometryParticleHandle*> FPBDSuspensionConstraintHandle::GetConstrainedParticles() const
	{
		return ConcreteContainer()->GetConstrainedParticles(ConstraintIndex);
	}

	void FPBDSuspensionConstraintHandle::GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		ConcreteContainer()->GatherInput(Dt, ConstraintIndex, Particle0Level, Particle1Level, SolverData);
	}


	Chaos::FPBDSuspensionConstraints::FConstraintContainerHandle* FPBDSuspensionConstraints::AddConstraint(TGeometryParticleHandle<FReal, 3>* Particle, const FVec3& InSuspensionLocalOffset, const FPBDSuspensionSettings& InConstraintSettings)
	{
		int32 NewIndex = ConstrainedParticles.Num();
		ConstrainedParticles.Add(Particle);
		SuspensionLocalOffset.Add(InSuspensionLocalOffset);
		ConstraintSettings.Add(InConstraintSettings);
		ConstraintResults.AddDefaulted();
		ConstraintEnabledStates.Add(true); // Note: assumes always enabled on creation
		ConstraintSolverBodies.Add(nullptr);
		Handles.Add(HandleAllocator.AllocHandle(this, NewIndex));
		return Handles[NewIndex];
	}


	void FPBDSuspensionConstraints::RemoveConstraint(int ConstraintIndex)
	{
		FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			if (ConstrainedParticles[ConstraintIndex])
			{
				ConstrainedParticles[ConstraintIndex]->RemoveConstraintHandle(ConstraintHandle);
			}

			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		ConstrainedParticles.RemoveAtSwap(ConstraintIndex);
		SuspensionLocalOffset.RemoveAtSwap(ConstraintIndex);
		ConstraintSettings.RemoveAtSwap(ConstraintIndex);
		ConstraintResults.RemoveAtSwap(ConstraintIndex);
		ConstraintEnabledStates.RemoveAtSwap(ConstraintIndex);
		ConstraintSolverBodies.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
		}
	}
	
	void FPBDSuspensionConstraints::SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData)
	{
		SolverData.GetConstraintIndices(ContainerId).Reset(NumIslandConstraints);
	}

	void FPBDSuspensionConstraints::GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		SolverData.GetConstraintIndices(ContainerId).Add(ConstraintIndex);

		ConstraintSolverBodies[ConstraintIndex] = SolverData.GetBodyContainer().FindOrAdd(ConstrainedParticles[ConstraintIndex]);

		ConstraintResults[ConstraintIndex].Reset();
	}

	void FPBDSuspensionConstraints::ScatterOutput(FReal Dt, FPBDIslandSolverData& SolverData)
	{
		for (int32 ConstraintIndex : SolverData.GetConstraintIndices(ContainerId))
		{
			ConstraintSolverBodies[ConstraintIndex] = nullptr;
		}
	}

	bool FPBDSuspensionConstraints::ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		for (int32 ConstraintIndex : SolverData.GetConstraintIndices(ContainerId))
		{
			ApplySingle(Dt, ConstraintIndex);
		}

		// @todo(chaos): early iteration termination in FPBDSuspensionConstraints
		return true;
	}

	void FPBDSuspensionConstraints::ApplySingle(const FReal Dt, int32 ConstraintIndex)
	{
		check(ConstraintSolverBodies[ConstraintIndex] != nullptr);
		FSolverBody& Body = *ConstraintSolverBodies[ConstraintIndex];
		const FPBDSuspensionSettings& Setting = ConstraintSettings[ConstraintIndex];
		FPBDSuspensionResults& Results = ConstraintResults[ConstraintIndex];

		if (Body.IsDynamic() && Setting.Enabled)
		{
			const FVec3& T = Setting.Target;

			// \todo(chaos): we can cache the CoM-relative connector once per frame rather than recalculate per iteration
			// (we should not be accessing particle state in the solver methods, although this one actually is ok because it only uses frame constrants)
			const FGenericParticleHandle Particle = ConstrainedParticles[ConstraintIndex];
			const FVec3& SuspensionActorOffset = SuspensionLocalOffset[ConstraintIndex];
			const FVec3 SuspensionCoMOffset = Particle->RotationOfMass().UnrotateVector(SuspensionActorOffset - Particle->CenterOfMass());
			const FVec3 SuspensionCoMAxis = Particle->RotationOfMass().UnrotateVector(Setting.Axis);

			const FRotation3 BodyQ = Body.CorrectedQ();
			const FVec3 BodyP = Body.CorrectedP();
			const FVec3 WorldSpaceX = BodyQ.RotateVector(SuspensionCoMOffset) + BodyP;

			FVec3 AxisWorld = BodyQ.RotateVector(SuspensionCoMAxis);

			const float MPHToCmS = 100000.f / 2236.94185f;
			const float SpeedThreshold = 10.0f * MPHToCmS;
			const float FortyFiveDegreesThreshold = 0.707f;

			if (AxisWorld.Z > FortyFiveDegreesThreshold)
			{
				if (Body.V().SquaredLength() < 1.0f)
				{
					AxisWorld = FVec3(0.f, 0.f, 1.f);
				}
				else
				{
					const FReal Speed = FMath::Abs(Body.V().Length());
					if (Speed < SpeedThreshold)
					{
						AxisWorld = FMath::Lerp(FVec3(0.f, 0.f, 1.f), AxisWorld, Speed / SpeedThreshold);
					}
				}
			}

			FReal Distance = FVec3::DotProduct(WorldSpaceX - T, AxisWorld);
			if (Distance >= Setting.MaxLength)
			{
				// do nothing since the target point is further than the longest extension of the suspension spring
				Results.Length = Setting.MaxLength;
				return;
			}

			FVec3 DX = FVec3::ZeroVector;

			// Require the velocity at the WorldSpaceX position - not the velocity of the particle origin
			const FVec3 Diff = WorldSpaceX - BodyP;
			FVec3 ArmVelocity = Body.V() - FVec3::CrossProduct(Diff, Body.W());

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
					const FReal SpringMassScale = (bAccelerationMode) ? FReal(1) / Body.InvM() : FReal(1);
					const FReal S = SpringMassScale * Setting.SpringStiffness * Dt * Dt;
					const FReal D = SpringMassScale * Setting.SpringDamping * Dt;
					DLambda = (S * SpringCompression - D * VelDt);
					DX += DLambda * AxisWorld;
				}
			}

			const FVec3 Arm = WorldSpaceX - BodyP;

			const FVec3 DP = Body.InvM() * DX;
			const FVec3 DR = Body.InvI() * FVec3::CrossProduct(Arm, DX);
			Body.ApplyTransformDelta(DP, DR);
			Body.UpdateRotationDependentState();

			Results.NetPushOut += DX;
			Results.Length = Distance;
		}
	}
	
	template class TIndexedContainerConstraintHandle<FPBDSuspensionConstraints>;


}
