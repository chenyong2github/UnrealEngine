// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"


// @todo(chaos): These classes should be in the unit testing suite, but we must currently explicitly instantiate the ConstraintRules in the Chaos unit
// because the template code is in a source file. We need to support custom constraints anyway and the NullConstraint could use that when it exists.

namespace Chaos
{
	class FPBDNullConstraintHandle;
	class FPBDNullConstraints;

	/**
	 * @brief A dummy constraint used for unit testing
	*/
	class FPBDNullConstraint
	{
	public:
		FPBDNullConstraint(const TVec2<FGeometryParticleHandle*>& InConstrainedParticles)
			: ConstrainedParticles(InConstrainedParticles)
			, bEnabled(true)
			, bSleeping(false)
		{
		}

		FParticlePair ConstrainedParticles;
		bool bEnabled;
		bool bSleeping;
	};

	/**
	 * Constraint Container with minimal API required to test the Graph.
	 */
	class FPBDNullConstraints : public FPBDIndexedConstraintContainer
	{
	public:
		using FConstraintContainerHandle = FPBDNullConstraintHandle;
		using FConstraintSolverContainerType = FConstraintSolverContainer;	// @todo(chaos): Add island solver for this constraint type

		FPBDNullConstraints();

		int32 NumConstraints() const { return Constraints.Num(); }

		FPBDNullConstraint& GetConstraint(const int32 ConstraintIndex)
		{
			return Constraints[ConstraintIndex];
		}

		const FPBDNullConstraint& GetConstraint(const int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		FPBDNullConstraintHandle* AddConstraint(const TVec2<FGeometryParticleHandle*>& InConstraintedParticles);

		FParticlePair GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex].ConstrainedParticles;
		}

		TArray<FPBDNullConstraintHandle*>& GetConstraintHandles()
		{
			return Handles;
		}

		const TArray<FPBDNullConstraintHandle*>& GetConstraintHandles() const
		{
			return Handles;
		}

		void PrepareTick() {}
		void UnprepareTick() {}
		void UpdatePositionBasedState(const FReal Dt) {}

		// Constraint Rule API
		void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData) {}

		// Simple Constraint Rule API
		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData) {}
		void GatherInput(const FReal Dt, FPBDIslandSolverData& SolverData) {}
		void ScatterOutput(const FReal Dt, FPBDIslandSolverData& SolverData) {}
		bool ApplyPhase1(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }
		bool ApplyPhase2(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }
		bool ApplyPhase3(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }

		// Island Constraint Rule API
		void PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData) {}
		void GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData) {}
		bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }
		bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }
		bool ApplyPhase3Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return true; }

		TArray<FPBDNullConstraint> Constraints;
		TArray<FPBDNullConstraintHandle*> Handles;
		TConstraintHandleAllocator<FPBDNullConstraints> HandleAllocator;
	};

	class FPBDNullConstraintHandle final : public TIndexedContainerConstraintHandle<FPBDNullConstraints>
	{
	public:
		FPBDNullConstraintHandle(FPBDNullConstraints* InConstraintContainer, int32 ConstraintIndex)
			: TIndexedContainerConstraintHandle<FPBDNullConstraints>(InConstraintContainer, ConstraintIndex)
		{
		}

		virtual void SetEnabled(bool bInEnabled)  override
		{
			ConcreteContainer()->GetConstraint(GetConstraintIndex()).bEnabled = bInEnabled;
		}

		virtual bool IsEnabled() const override
		{
			return ConcreteContainer()->GetConstraint(GetConstraintIndex()).bEnabled;
		}

		virtual void SetIsSleeping(bool bInIsSleeping)  override
		{
			ConcreteContainer()->GetConstraint(GetConstraintIndex()).bSleeping = bInIsSleeping;
		}

		virtual bool IsSleeping() const override
		{
			return ConcreteContainer()->GetConstraint(GetConstraintIndex()).bSleeping;
		}

		virtual FParticlePair GetConstrainedParticles() const override
		{
			return ConcreteContainer()->GetConstrainedParticles(GetConstraintIndex());
		}

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData)
		{
			ConcreteContainer()->PreGatherInput(Dt, GetConstraintIndex(), SolverData);
		}

		void GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
		{
			ConcreteContainer()->GatherInput(Dt, GetConstraintIndex(), Particle0Level, Particle1Level, SolverData);
		}

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FPBDNullConstraintHandle"), &FIndexedConstraintHandle::StaticType());
			return STypeID;
		}
	};


	inline FPBDNullConstraints::FPBDNullConstraints()
		: FPBDIndexedConstraintContainer(FPBDNullConstraintHandle::StaticType())
	{
	}

	inline FPBDNullConstraintHandle* FPBDNullConstraints::AddConstraint(const TVec2<FGeometryParticleHandle*>& InConstraintedParticles)
	{
		const int32 ConstraintIndex = Constraints.Emplace(FPBDNullConstraint(InConstraintedParticles));
		const int32 HandleIndex = Handles.Emplace(HandleAllocator.AllocHandle(this, ConstraintIndex));
		check(ConstraintIndex == HandleIndex);
		return Handles[HandleIndex];
	}

}