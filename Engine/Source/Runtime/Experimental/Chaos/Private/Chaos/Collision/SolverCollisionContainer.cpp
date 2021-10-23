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
	}
	using namespace CVars;

	extern bool bChaos_Collision_Manifold_FixNormalsInWorldSpace;


	/**
	 * @brief A wrapper for FPBDCollisionSolver which binds to a Collision Constraint and adds Gather/Scatter from/to the constraint
	*/
	class FPBDCollisionSolverAdapter
	{
	public:
		FPBDCollisionSolverAdapter()
			: Constraint(nullptr)
		{
		}

		FPBDCollisionSolver& GetSolver() { return Solver; }
		FPBDCollisionConstraint* GetConstraint() { return Constraint; }

		bool IsIncrementalManifold() const { return Constraint->GetUseIncrementalCollisionDetection(); }

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

			// We should try to remove this
			Constraint->SetSolverBodies(Body0, Body1);
		}

		void GatherManifoldPoints(const FReal Dt)
		{
			// We handle incremental manifolds by just colecting any new contacts
			const int32 BeginPointIndex = Solver.NumManifoldPoints();
			const int32 EndPointIndex = FMath::Min(Constraint->GetManifoldPoints().Num(), FPBDCollisionSolver::MaxPointsPerConstraint);

			Solver.SetNumManifoldPoints(EndPointIndex);

			const FReal RestitutionVelocityThreshold = Constraint->Manifold.RestitutionThreshold * Dt;
			const FReal Restitution = Constraint->Manifold.Restitution;

			for (int32 ManifoldPointIndex = BeginPointIndex; ManifoldPointIndex < EndPointIndex; ++ManifoldPointIndex)
			{
				const FManifoldPoint& ManifoldPoint = Constraint->GetManifoldPoints()[ManifoldPointIndex];

				// The world-space contact normal
				FVec3 WorldContactNormal;
				if (bChaos_Collision_Manifold_FixNormalsInWorldSpace)
				{
					WorldContactNormal = ManifoldPoint.ManifoldContactNormal;
				}
				else
				{
					const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Solver.SolverBody0().Q() : Solver.SolverBody1().Q();
					WorldContactNormal = PlaneQ * ManifoldPoint.ManifoldContactNormal;
				}

				// Initialize the structural data in the contact (relative contact points, contact mass etc)
				Solver.InitContact(
					ManifoldPointIndex,
					ManifoldPoint.CoMAnchorPoints[0],
					ManifoldPoint.CoMAnchorPoints[1],
					WorldContactNormal);

				// Calculate the target normal velocity based on restitution
				FReal WorldContactVelocityTargetNormal = FReal(0);
				if (Restitution > FReal(0))
				{
					const FVec3 ContactVelocity = Solver.GetManifoldPoint(ManifoldPointIndex).CalculateContactVelocity(Solver.SolverBody0(), Solver.SolverBody1());
					const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, WorldContactNormal);
					if (ContactVelocityNormal < -RestitutionVelocityThreshold)
					{
						WorldContactVelocityTargetNormal = -Restitution * ContactVelocityNormal;
					}
				}

				// Initialize the material properties (restitution and friction related)
				Solver.InitMaterial(
					ManifoldPointIndex,
					WorldContactVelocityTargetNormal,
					ManifoldPoint.bPotentialRestingContact && (Solver.StaticFriction() > 0),
					ManifoldPoint.StaticFrictionMax);
			}
		}

		/**
		 * @brief Send all solver results to the constraint
		*/
		void ScatterOutput(
			const FReal Dt)
		{
			FVec3 AccumulatedImpulse = FVec3(0);

			for (int32 PointIndex = 0; PointIndex < Solver.NumManifoldPoints(); ++PointIndex)
			{
				const FPBDCollisionSolver::FSolverManifoldPoint& SolverManifoldPoint = Solver.GetManifoldPoint(PointIndex);
				FManifoldPoint& ManifoldPoint = Constraint->GetManifoldPoints()[PointIndex];

				ManifoldPoint.NetPushOut = SolverManifoldPoint.NetPushOut;
				ManifoldPoint.NetPushOutNormal = FVec3::DotProduct(SolverManifoldPoint.NetPushOut, SolverManifoldPoint.WorldContactNormal);
				ManifoldPoint.NetImpulse = SolverManifoldPoint.NetImpulse;
				ManifoldPoint.NetImpulseNormal = FVec3::DotProduct(SolverManifoldPoint.NetImpulse, SolverManifoldPoint.WorldContactNormal);
				ManifoldPoint.bActive = !SolverManifoldPoint.NetPushOut.IsNearlyZero();
				ManifoldPoint.bInsideStaticFrictionCone = SolverManifoldPoint.bInsideStaticFrictionCone;

				// If the contact has no static friction, reset the static friction point to the current contact position
				// @todo(chaos): this should keep the prev contact at the friction cone edge, rather than just resetting 
				// (this currently causes stick-slide movement just above the friction threshold)
				if (!ManifoldPoint.bActive || !ManifoldPoint.bInsideStaticFrictionCone)
				{
					ManifoldPoint.CoMAnchorPoints[0] = ManifoldPoint.CoMContactPoints[0];
					ManifoldPoint.CoMAnchorPoints[1] = ManifoldPoint.CoMContactPoints[1];
				}

				AccumulatedImpulse += SolverManifoldPoint.NetImpulse + (SolverManifoldPoint.NetPushOut / Dt);
			}

			Constraint->AccumulatedImpulse = AccumulatedImpulse;

			Constraint->SetSolverBodies(nullptr, nullptr);
			Constraint = nullptr;
			Solver.ResetSolverBodies();
		}


	private:
		FPBDCollisionSolver Solver;
		FPBDCollisionConstraint* Constraint;
	};


	FPBDCollisionSolverContainer::FPBDCollisionSolverContainer()
		: FConstraintSolverContainer()
	{
	}

	FPBDCollisionSolverContainer::~FPBDCollisionSolverContainer()
	{
	}

	void FPBDCollisionSolverContainer::Reset(const int32 MaxCollisions)
	{
		CollisionSolvers.Reset(MaxCollisions);
		SweptCollisionSolvers.Reset();
	}

	FPBDCollisionSolver* FPBDCollisionSolverContainer::AddConstraintSolver(FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FSolverBodyContainer& SolverBodyContainer)
	{
		// This container is required to allocate pointers that are valid for the whole tick,
		// so we cannot allow the container to resize during the tick. See Reset()
		check(CollisionSolvers.Num() < CollisionSolvers.Max());

		const int32 SolverCollisionIndex = CollisionSolvers.AddDefaulted();

		CollisionSolvers[SolverCollisionIndex].GatherInput(Dt, Constraint, Particle0Level, Particle1Level, SolverBodyContainer);

		// Keep a list of CCD contacts - they will get resolved before we move on to the main solver loop
		if (Constraint.GetType() == ECollisionConstraintType::Swept)
		{
			SweptCollisionSolvers.Add(SolverCollisionIndex);
		}

		return &CollisionSolvers[SolverCollisionIndex].GetSolver();
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

	void FPBDCollisionSolverContainer::SolveSwept(const FReal Dt)
	{
		for (int32 SweptSolverCollisionIndex : SweptCollisionSolvers)
		{
			SolveSwept(Dt, *CollisionSolvers[SweptSolverCollisionIndex].GetConstraint(), CollisionSolvers[SweptSolverCollisionIndex].GetSolver());
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

	void FPBDCollisionSolverContainer::SolveSwept(const FReal Dt, FPBDCollisionConstraint& Constraint, FPBDCollisionSolver& CollisionSolver)
	{
		// @todo(chaos): Fix CCD. This "solution" just rewinds the CCD object to the first contact and will result in lost momentum
		if (Constraint.TimeOfImpact < FReal(1))
		{
			Collisions::UpdateSwept(Constraint, Dt);

			// Just rewind the body to the impact. This is not good at all, but it will prevent penetration for now
			FSolverBody& Body0 = CollisionSolver.SolverBody0().SolverBody();
			Body0.SetP(FMath::Lerp(Body0.X(), Body0.P(), Constraint.TimeOfImpact));
		}
	}

	bool FPBDCollisionSolverContainer::SolvePositionImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		UpdatePositionShockPropagation(Dt, It, NumIts, BeginIndex, EndIndex);

		const int32 NumZeroFrictionIterations = Chaos_PBDCollisionSolver_Position_ZeroFrictionIterations;
		const bool bApplyStaticFriction = (It >= NumZeroFrictionIterations);

		// Apply the position correction
		// @todo(chaos): parallel version of SolvePosition
		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];

			// @todo(chaos): remove this when we no longer need to support incremental manifolds - this prevents us from fully optimizing the solver loop
			if (CollisionSolver.IsIncrementalManifold())
			{
				Collisions::Update(*CollisionSolver.GetConstraint(), Dt);
				CollisionSolver.GatherManifoldPoints(Dt);
			}

			bNeedsAnotherIteration |= CollisionSolvers[SolverIndex].GetSolver().SolvePosition(Dt, bApplyStaticFriction);
		}

		return bNeedsAnotherIteration;
	}

	bool FPBDCollisionSolverContainer::SolveVelocityImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

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
