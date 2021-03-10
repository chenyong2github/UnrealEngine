// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
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

	class CHAOS_API FPBDSuspensionConstraintHandle : public TContainerConstraintHandle<FPBDSuspensionConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDSuspensionConstraints>;
		using FConstraintContainer = FPBDSuspensionConstraints;

		FPBDSuspensionConstraintHandle() {}
		FPBDSuspensionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);
		static FConstraintHandle::EType StaticType() { return FConstraintHandle::EType::Suspension; }

		FPBDSuspensionSettings& GetSettings();
		const FPBDSuspensionSettings& GetSettings() const;

		void SetSettings(const FPBDSuspensionSettings& Settings);

		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	class CHAOS_API FPBDSuspensionConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;
		using FConstraintContainerHandle = FPBDSuspensionConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDSuspensionConstraints>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBDSuspensionConstraints(const FPBDSuspensionSolverSettings& InSolverSettings = FPBDSuspensionSolverSettings())
			: SolverSettings(InSolverSettings)
		{}

		FPBDSuspensionConstraints(TArray<FVec3>&& Locations, TArray<TGeometryParticleHandle<FReal,3>*>&& InConstrainedParticles, TArray<FVec3>&& InLocalOffset, TArray<FPBDSuspensionSettings>&& InConstraintSettings)
			: ConstrainedParticles(MoveTemp(InConstrainedParticles)), SuspensionLocalOffset(MoveTemp(InLocalOffset)), ConstraintSettings(MoveTemp(InConstraintSettings))
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


		/**
		 * Disabled the specified constraint.
		 */
		void DisableConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
		{
			for (TGeometryParticleHandle<FReal, 3>* RemovedParticle : RemovedParticles)
			{
				for (FConstraintHandle* ConstraintHandle : RemovedParticle->ParticleConstraints())
				{
					ConstraintHandle->SetEnabled(false); // constraint lifespan is managed by the proxy
				}
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

		void PrepareIteration(FReal Dt) {}

		void UnprepareIteration(FReal Dt) {}

		void UpdatePositionBasedState(const FReal Dt) {}

		bool Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& ConstraintHandles, const int32 It, const int32 NumIts) const;

		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintIndices, const int32 It, const int32 NumIts) const
		{
			return false;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:

		void ApplySingle(const FReal Dt, int32 ConstraintIndex) const;
		
		void ApplyPositionConstraintSoft(const int ConstraintIndex, const FReal Dt, const bool bAccelerationMode) const;
		FPBDSuspensionSolverSettings SolverSettings;

		TArray<FGeometryParticleHandle*> ConstrainedParticles;
		TArray<FVec3> SuspensionLocalOffset;
		TArray<FPBDSuspensionSettings> ConstraintSettings;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}

//PRAGMA_ENABLE_OPTIMIZATION