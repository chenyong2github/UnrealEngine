// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/SolverCollisionContainer.h"

#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Utilities.h"

// Private includes
#include "PBDCollisionSolver.h"

#include "ChaosLog.h"


namespace Chaos
{
	namespace CVars
	{
		extern int32 Chaos_PBDCollisionSolver_Position_ZeroFrictionIterations;
		extern int32 Chaos_PBDCollisionSolver_Position_ShockPropagationIterations;

		extern int32 Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations;

		extern bool bChaos_PBDCollisionSolver_Position_SolveEnabled;
		extern bool bChaos_PBDCollisionSolver_Velocity_SolveEnabled;
	}
	using namespace CVars;

	extern bool bChaos_Collision_Manifold_FixNormalsInWorldSpace;


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	/**
	 * @brief A wrapper for FPBDCollisionSolver which binds to a Collision Constraint and adds Gather/Scatter from/to the constraint
	*/
	class FPBDCollisionSolverAdapter
	{
	public:
		FPBDCollisionSolverAdapter()
			: Constraint(nullptr)
			, bIsIncremental(false)
		{
		}

		FPBDCollisionSolver& GetSolver() { return Solver; }
		FPBDCollisionConstraint* GetConstraint() { return Constraint; }

		bool IsIncrementalManifold() const { return bIsIncremental; }

		/**
		 * @brief Initialize the data required for the solver, and bind to the bodies
		*/
		void GatherInput(
			const FReal Dt,
			FPBDCollisionConstraint& InConstraint,
			const int32 Particle0Level,
			const int32 Particle1Level,
			FSolverBodyContainer& SolverBodyContainer)
		{
			Constraint = &InConstraint;
			bIsIncremental = Constraint->GetUseIncrementalCollisionDetection();

			// Find the solver bodies for the particles we constrain. This will add them to the container
			// if they aren't there already, and ensure that they are populated with the latest data.
			FSolverBody* Body0 = SolverBodyContainer.FindOrAdd(Constraint->Particle[0]);
			FSolverBody* Body1 = SolverBodyContainer.FindOrAdd(Constraint->Particle[1]);

			Body0->SetLevel(Particle0Level);
			Body1->SetLevel(Particle1Level);

			Solver.SetFriction(
				FMath::Max(Constraint->Manifold.AngularFriction, Constraint->Manifold.Friction),
				Constraint->Manifold.Friction);

			Solver.SetStiffness(Constraint->GetStiffness());

			Solver.SetSolverBodies(Body0, Body1);

			GatherManifoldPoints(Dt);

			// We should try to remove this - the Constraint should not need to know about solver objects
			Constraint->SetSolverBodies(Body0, Body1);
		}

		void GatherManifoldPoints(const FReal Dt)
		{
			// We handle incremental manifolds by just collecting any new contacts
			const int32 BeginPointIndex = Solver.NumManifoldPoints();
			const int32 EndPointIndex = Solver.SetNumManifoldPoints(Constraint->GetManifoldPoints().Num());

			const FReal RestitutionVelocityThreshold = Constraint->Manifold.RestitutionThreshold * Dt;
			const FReal Restitution = Constraint->Manifold.Restitution;

			for (int32 ManifoldPointIndex = BeginPointIndex; ManifoldPointIndex < EndPointIndex; ++ManifoldPointIndex)
			{
				TArrayView<FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();
				const FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

				// Initialize the structural data in the contact (relative contact points, contact mass etc)
				Solver.InitContact(
					ManifoldPointIndex,
					ManifoldPoint.WorldContactPoints[0],
					ManifoldPoint.WorldContactPoints[1],
					ManifoldPoint.ContactPoint.Normal);

				// Initialize the material properties (restitution and friction related)
				Solver.InitMaterial(
					ManifoldPointIndex,
					Restitution,
					RestitutionVelocityThreshold,
					(Solver.StaticFriction() > 0),
					ManifoldPoint.StaticFrictionMax);
			}
		}

		/**
		 * @brief Send all solver results to the constraint
		*/
		void ScatterOutput(const FReal Dt)
		{
			FVec3 AccumulatedImpulse = FVec3(0);

			Constraint->ResetSolverResults();
			Constraint->SetNumActivePositionIterations(Solver.NumPositionSolves());

			for (int32 PointIndex = 0; PointIndex < Solver.NumManifoldPoints(); ++PointIndex)
			{
				const FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = Solver.GetManifoldPoint(PointIndex);
				Constraint->SetSolverResults(PointIndex, 
					SolverManifoldPoint.NetPushOut, 
					SolverManifoldPoint.NetImpulse, 
					SolverManifoldPoint.bInsideStaticFrictionCone,
					SolverManifoldPoint.StaticFrictionMax,
					Dt);
			}

			Constraint->SetSolverBodies(nullptr, nullptr);
			Constraint = nullptr;
			Solver.ResetSolverBodies();
		}


	private:
		FPBDCollisionSolver Solver;
		FPBDCollisionConstraint* Constraint;
		bool bIsIncremental;
	};


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FPBDCollisionSolverContainer::FPBDCollisionSolverContainer()
		: FConstraintSolverContainer()
		, MaxPushOutVelocity(0)
		, bRequiresIncrementalCollisionDetection(false)
	{
	}

	FPBDCollisionSolverContainer::~FPBDCollisionSolverContainer()
	{
	}

	void FPBDCollisionSolverContainer::Reset(const int32 MaxCollisions)
	{
		CollisionSolvers.Reset(MaxCollisions);
	}

	void FPBDCollisionSolverContainer::AddConstraintSolver(FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FSolverBodyContainer& SolverBodyContainer)
	{
		// This container is required to allocate pointers that are valid for the whole tick,
		// so we cannot allow the container to resize during the tick. See Reset()
		check(CollisionSolvers.Num() < CollisionSolvers.Max());

		const int32 SolverCollisionIndex = CollisionSolvers.AddDefaulted();

		CollisionSolvers[SolverCollisionIndex].GatherInput(Dt, Constraint, Particle0Level, Particle1Level, SolverBodyContainer);

		bRequiresIncrementalCollisionDetection |= CollisionSolvers[SolverCollisionIndex].IsIncrementalManifold();
	}

	void FPBDCollisionSolverContainer::UpdatePositionShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		// If this is the first shock propagation iteration, enable it on each solver
		const int32 NumShockPropIterations = Chaos_PBDCollisionSolver_Position_ShockPropagationIterations;
		const bool bEnableShockPropagation = (It == NumIts - NumShockPropIterations);
		if (bEnableShockPropagation)
		{
			for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().EnablePositionShockPropagation();
			}
		}
	}

	void FPBDCollisionSolverContainer::UpdateVelocityShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		// Set/reset the shock propagation based on current iteration. The position solve may
		// have left the bodies with a mass scale and we want to change or reset it.
		const int32 NumShockPropIterations = Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations;
		const bool bEnableShockPropagation = (It == NumIts - NumShockPropIterations);
		if (bEnableShockPropagation)
		{
			for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().EnableVelocityShockPropagation();
			}
		}
		else if (It == 0)
		{
			for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().DisableShockPropagation();
			}
		}
	}

	bool FPBDCollisionSolverContainer::SolvePositionSerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		return SolvePositionImpl(Dt, It, NumIts, BeginIndex, EndIndex, false);
	}

	bool FPBDCollisionSolverContainer::SolveVelocitySerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		return SolveVelocityImpl(Dt, It, NumIts, BeginIndex, EndIndex, false);
	}

	bool FPBDCollisionSolverContainer::SolvePositionParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		return SolvePositionImpl(Dt, It, NumIts, BeginIndex, EndIndex, true);
	}

	bool FPBDCollisionSolverContainer::SolveVelocityParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex)
	{
		return SolveVelocityImpl(Dt, It, NumIts, BeginIndex, EndIndex, true);
	}

	// @todo(chaos): parallel version of SolvePosition
	bool FPBDCollisionSolverContainer::SolvePositionImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);
		if (!bChaos_PBDCollisionSolver_Position_SolveEnabled)
		{
			return false;
		}

		UpdatePositionShockPropagation(Dt, It, NumIts, BeginIndex, EndIndex);

		const int32 NumZeroFrictionIterations = Chaos_PBDCollisionSolver_Position_ZeroFrictionIterations;
		const bool bApplyStaticFriction = (It >= NumZeroFrictionIterations);
		const FReal MaxPushOut = (MaxPushOutVelocity > 0) ? (MaxPushOutVelocity * Dt) / FReal(NumIts) : 0;

		// Apply the position correction
		if (bRequiresIncrementalCollisionDetection)
		{
			return SolvePositionIncrementalImpl(Dt, BeginIndex, EndIndex, MaxPushOut, bApplyStaticFriction);
		}
		else if (bApplyStaticFriction)
		{
			return SolvePositionWithFrictionImpl(Dt, BeginIndex, EndIndex, MaxPushOut);
		}
		else
		{
			return SolvePositionNoFrictionImpl(Dt, BeginIndex, EndIndex, MaxPushOut);
		}
	}

	// Solve position including support for incremental collision detection
	bool FPBDCollisionSolverContainer::SolvePositionIncrementalImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut, const bool bApplyStaticFriction)
	{
		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];
			if (CollisionSolver.IsIncrementalManifold())
			{
				Collisions::Update(*CollisionSolver.GetConstraint(), Dt);
				CollisionSolver.GatherManifoldPoints(Dt);
			}
			if (bApplyStaticFriction)
			{
				bNeedsAnotherIteration |= CollisionSolver.GetSolver().SolvePositionWithFriction(Dt, MaxPushOut);
			}
			else
			{
				bNeedsAnotherIteration |= CollisionSolver.GetSolver().SolvePositionNoFriction(Dt, MaxPushOut);
			}

		}
		return bNeedsAnotherIteration;
	}

	// Solve position with friction (last few iterations each tick)
	bool FPBDCollisionSolverContainer::SolvePositionWithFrictionImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut)
	{
		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			bNeedsAnotherIteration |= CollisionSolvers[SolverIndex].GetSolver().SolvePositionWithFriction(Dt, MaxPushOut);
		}
		return bNeedsAnotherIteration;
	}

	// Solve position without friction (first few iterations each tick)
	bool FPBDCollisionSolverContainer::SolvePositionNoFrictionImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut)
	{
		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			bNeedsAnotherIteration |= CollisionSolvers[SolverIndex].GetSolver().SolvePositionNoFriction(Dt, MaxPushOut);
		}
		return bNeedsAnotherIteration;
	}

	bool FPBDCollisionSolverContainer::SolveVelocityImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);
		if (!bChaos_PBDCollisionSolver_Velocity_SolveEnabled)
		{
			return false;
		}

		UpdateVelocityShockPropagation(Dt, It, NumIts, BeginIndex, EndIndex);

		const int32 NumDynamicFrictionIterations = 1;
		const bool bApplyDynamicFriction = (It >= NumIts - NumDynamicFrictionIterations);

		// Apply the velocity correction
		// @todo(chaos): parallel version of SolveVelocity
		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];

			bNeedsAnotherIteration |= CollisionSolver.GetSolver().SolveVelocity(Dt, bApplyDynamicFriction);
		}

		return bNeedsAnotherIteration;
	}

	void FPBDCollisionSolverContainer::ScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex)
	{
		return ScatterOutputImpl(Dt, BeginIndex, EndIndex, false);
	}

	void FPBDCollisionSolverContainer::ScatterOutputImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Scatter);
		check(BeginIndex >= 0);
		check(EndIndex <= CollisionSolvers.Num());

		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			CollisionSolvers[SolverIndex].ScatterOutput(Dt);
		}
	}

}
