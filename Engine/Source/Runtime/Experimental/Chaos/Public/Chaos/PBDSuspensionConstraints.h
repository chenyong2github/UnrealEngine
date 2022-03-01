// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/Evolution/SolverDatas.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDSuspensionConstraintTypes.h"
#include "Chaos/PBDSuspensionConstraintData.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	class FPBDSuspensionConstraints;
	class FPBDIslandSolverData;
	class FPBDCollisionSolver;

	class CHAOS_API FPBDSuspensionConstraintHandle : public TIndexedContainerConstraintHandle<FPBDSuspensionConstraints>
	{
	public:
		using Base = TIndexedContainerConstraintHandle<FPBDSuspensionConstraints>;
		using FConstraintContainer = FPBDSuspensionConstraints;

		FPBDSuspensionConstraintHandle() {}
		FPBDSuspensionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		FPBDSuspensionSettings& GetSettings();
		const FPBDSuspensionSettings& GetSettings() const;

		void SetSettings(const FPBDSuspensionSettings& Settings);

		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const;

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FSuspensionConstraintHandle"), &FIndexedConstraintHandle::StaticType());
			return STypeID;
		}
	protected:
		using Base::ConstraintIndex;
		using Base::ConcreteContainer;
	};

	class CHAOS_API FPBDSuspensionConstraints : public FPBDIndexedConstraintContainer
	{
	public:
		using Base = FPBDIndexedConstraintContainer;
		using FConstraintContainerHandle = FPBDSuspensionConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDSuspensionConstraints>;
		using FHandles = TArray<FConstraintContainerHandle*>;
		using FConstraintSolverContainerType = FConstraintSolverContainer;	// @todo(chaos): Add island solver for this constraint type

		FPBDSuspensionConstraints(const FPBDSuspensionSolverSettings& InSolverSettings = FPBDSuspensionSolverSettings())
			: FPBDIndexedConstraintContainer(FConstraintContainerHandle::StaticType())
			, SolverSettings(InSolverSettings)
		{}

		FPBDSuspensionConstraints(TArray<FVec3>&& Locations, TArray<TGeometryParticleHandle<FReal,3>*>&& InConstrainedParticles, TArray<FVec3>&& InLocalOffset, TArray<FPBDSuspensionSettings>&& InConstraintSettings)
			: FPBDIndexedConstraintContainer(FConstraintContainerHandle::StaticType())
			, ConstrainedParticles(MoveTemp(InConstrainedParticles)), SuspensionLocalOffset(MoveTemp(InLocalOffset)), ConstraintSettings(MoveTemp(InConstraintSettings))
		{
			if (ConstrainedParticles.Num() > 0)
			{
				Handles.Reserve(ConstrainedParticles.Num());
				for (int32 ConstraintIndex = 0; ConstraintIndex < ConstrainedParticles.Num(); ++ConstraintIndex)
				{
					Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
				}
			}
		}

		virtual ~FPBDSuspensionConstraints() {}

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const
		{
			return ConstrainedParticles.Num();
		}

		/**
		 * Add a constraint.
		 */
		FConstraintContainerHandle* AddConstraint(TGeometryParticleHandle<FReal, 3>* Particle, const FVec3& InConstraintFrame, const FPBDSuspensionSettings& InConstraintSettings);

		/**
		 * Remove a constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);


		/*
		* Disconnect the constraints from the attached input particles.
		* This will set the constrained Particle elements to nullptr and
		* set the Enable flag to false.
		*
		* The constraint is unuseable at this point and pending deletion.
		*/

		void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
		{
			for (FGeometryParticleHandle* RemovedParticle : RemovedParticles)
			{
				for (FConstraintHandle* ConstraintHandle : RemovedParticle->ParticleConstraints())
				{
					if (FPBDSuspensionConstraintHandle* SuspensionHandle = ConstraintHandle->As<FPBDSuspensionConstraintHandle>())
					{
						SuspensionHandle->SetEnabled(false); // constraint lifespan is managed by the proxy

						int ConstraintIndex = SuspensionHandle->GetConstraintIndex();
						if (ConstraintIndex != INDEX_NONE)
						{
							if (ConstrainedParticles[ConstraintIndex] == RemovedParticle)
							{
								ConstrainedParticles[ConstraintIndex] = nullptr;
							}
						}
					}
				}
			}
		}

		bool IsConstraintEnabled(int32 ConstraintIndex) const
		{
			return ConstraintEnabledStates[ConstraintIndex];
		}

		void SetConstraintEnabled(int32 ConstraintIndex, bool bEnabled)
		{
			const FGenericParticleHandle Particle = FGenericParticleHandle(ConstrainedParticles[ConstraintIndex]);

			if (bEnabled)
			{
				// only enable constraint if the particle is valid and not disabled
				if (Particle->Handle() != nullptr && !Particle->Disabled())
				{
					ConstraintEnabledStates[ConstraintIndex] = true;
				}
			}
			else
			{
				// desirable to allow disabling no matter what state the endpoint
				ConstraintEnabledStates[ConstraintIndex] = false;
			}

		}

		//
		// Constraint API
		//
		const FPBDSuspensionSettings& GetSettings(int32 ConstraintIndex) const
		{
			return ConstraintSettings[ConstraintIndex];
		}

		FPBDSuspensionSettings& GetSettings(int32 ConstraintIndex)
		{
			return ConstraintSettings[ConstraintIndex];
		}

		void SetSettings(int32 ConstraintIndex, const FPBDSuspensionSettings& Settings)
		{
			ConstraintSettings[ConstraintIndex] = Settings;
		}

		void SetTarget(int32 ConstraintIndex, const FVector& TargetPos)
		{
			ConstraintSettings[ConstraintIndex].Target = TargetPos;
		}

		const FPBDSuspensionResults& GetResults(int32 ConstraintIndex) const
		{
			return ConstraintResults[ConstraintIndex];
		}

		FHandles& GetConstraintHandles()
		{
			return Handles;
		}
		const FHandles& GetConstConstraintHandles() const
		{
			return Handles;
		}

		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const
		{
			return Handles[ConstraintIndex];
		}

		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex)
		{
			return Handles[ConstraintIndex];
		}

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		TVec2<TGeometryParticleHandle<FReal, 3>*> GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return { ConstrainedParticles[ConstraintIndex], nullptr };
		}

		/**
		 * Get the world-space constraint positions for each body.
		 */
		const FVec3& GetConstraintPosition(int ConstraintIndex) const
		{
			return SuspensionLocalOffset[ConstraintIndex];
		}

		void SetConstraintPosition(const int32 ConstraintIndex, const FVec3& Position)
		{
			SuspensionLocalOffset[ConstraintIndex] = Position;
		}

		//
		// Island Rule API
		//

		void PrepareTick() {}
		void UnprepareTick() {}
		void UpdatePositionBasedState(const FReal Dt) {}

		void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData);
		void PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);
		void ScatterOutput(FReal Dt, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:

		void ApplySingle(const FReal Dt, int32 ConstraintIndex);
		
		void ApplyPositionConstraintSoft(const int ConstraintIndex, const FReal Dt, const bool bAccelerationMode);
		FPBDSuspensionSolverSettings SolverSettings;

		TArray<FGeometryParticleHandle*> ConstrainedParticles;
		TArray<FVec3> SuspensionLocalOffset;
		TArray<FPBDSuspensionSettings> ConstraintSettings;
		TArray<FPBDSuspensionResults> ConstraintResults;
		TArray<bool> ConstraintEnabledStates;

		TArray<FSolverBody*> ConstraintSolverBodies;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;

		TArray<FPBDCollisionSolver*> CollisionSolvers;
		TArray<FSolverBody> StaticCollisionBodies;
	};
}

//PRAGMA_ENABLE_OPTIMIZATION
