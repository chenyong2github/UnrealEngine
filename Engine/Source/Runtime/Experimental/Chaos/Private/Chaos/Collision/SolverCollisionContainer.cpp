// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/SolverCollisionContainer.h"

#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Utilities.h"

// Private includes
#include "PBDCollisionSolver.h"

#include "ChaosLog.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace CVars
	{
		extern bool bChaos_PBDCollisionSolver_Position_SolveEnabled;
		extern bool bChaos_PBDCollisionSolver_Velocity_SolveEnabled;

		// If one body is more than MassRatio1 times the mass of the other, adjust the solver stiffness when the lighter body is underneath.
		// Solver stiffness will be equal to 1 when the mass ratio is MassRatio1.
		// Solver stiffness will be equal to 0 when the mass ratio is MassRatio2.
		FRealSingle Chaos_PBDCollisionSolver_AutoStiffness_MassRatio1 = 0;
		FRealSingle Chaos_PBDCollisionSolver_AutoStiffness_MassRatio2 = 0;
		FAutoConsoleVariableRef CVarChaosPBDCollisionSolverAutoStiffnessMassRatio1(TEXT("p.Chaos.PBDCollisionSolver.AutoStiffness.MassRatio1"), Chaos_PBDCollisionSolver_AutoStiffness_MassRatio1, TEXT(""));
		FAutoConsoleVariableRef CVarChaosPBDCollisionSolverAutoStiffnessMassRatio2(TEXT("p.Chaos.PBDCollisionSolver.AutoStiffness.MassRatio2"), Chaos_PBDCollisionSolver_AutoStiffness_MassRatio2, TEXT(""));
	}
	using namespace CVars;


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FPBDCollisionSolverSettings::FPBDCollisionSolverSettings()
		: MaxPushOutVelocity(0)
		, NumPositionFrictionIterations(4)
		, NumVelocityFrictionIterations(1)
		, NumPositionShockPropagationIterations(3)
		, NumVelocityShockPropagationIterations(1)
	{
	}


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
			, bIsManifold(true)
			, bIsIncrementalManifold(false)
		{
		}

		FPBDCollisionSolver& GetSolver() { return Solver; }
		FPBDCollisionConstraint* GetConstraint() { return Constraint; }

		bool IsManifold() const { return bIsManifold; }
		bool IsIncrementalManifold() const { return bIsIncrementalManifold; }

		void PreGatherInput(
			const FReal Dt,
			FPBDCollisionConstraint& InConstraint,
			FSolverBodyContainer& SolverBodyContainer)
		{
			Constraint = &InConstraint;
			SolverBodyContainer.FindOrAdd(Constraint->GetParticle0(), Dt);
			SolverBodyContainer.FindOrAdd(Constraint->GetParticle1(), Dt);
		}

		/**
		 * @brief Modify solver stiffness when we have bodies with large mass differences
		*/
		FReal CalculateSolverStiffness(
			const FSolverBody* Body0,
			const FSolverBody* Body1,
			const FReal MassRatio1,
			const FReal MassRatio2)
		{
			// Adjust the solver stiffness if one body is more than MassRatio1 times the mass of the other and the heavier one is on top.
			// Solver stiffness will be equal to 1 when the mass ratio is MassRatio1.
			// Solver stiffness will be equal to 0 when the mass ratio is MassRatio2.
			if (Body0->IsDynamic() && Body1->IsDynamic() && (MassRatio1 > 0) && (MassRatio2 > MassRatio1))
			{
				// Find heavy body and the mass ratio
				const FSolverBody* HeavyBody;
				FReal MassRatio;
				if (Body0->InvM() < Body1->InvM())
				{
					HeavyBody = Body0;
					MassRatio = Body1->InvM() / Body0->InvM();
				}
				else
				{
					HeavyBody = Body1;
					MassRatio = Body0->InvM() / Body1->InvM();
				}

				if (MassRatio > MassRatio1)
				{
					// Is this a load-bearing contact (normal is significantly along gravity direction)?
					// @todo(chaos): should use gravity direction. Currently assumes -Z
					// @todo(chaos): maybe gradually introduce stiffness scaling based on normal rather than on/off
					// @todo(chaos): could use solver manifold data which is already in world space rather than CalculateWorldContactNormal
					const FVec3 WorldNormal = Constraint->CalculateWorldContactNormal();
					if (FMath::Abs(WorldNormal.Z) > FReal(0.3))
					{
						// Which body is on the top? (Normal always points away from second body - see FContactPoint)
						const FSolverBody* TopBody = (WorldNormal.Z > 0) ? Body0 : Body1;
						if (TopBody == HeavyBody)
						{
							// The heavy body is on top - reduce the solver stiffness
							const FReal StiffnessScale = FMath::Clamp((MassRatio2 - MassRatio) / (MassRatio2 - MassRatio1), FReal(0), FReal(1));
							return StiffnessScale * Constraint->GetStiffness();
						}
					}
				}

			}

			return Constraint->GetStiffness();
		}

		void BindBodies(
			const FReal Dt,
			const int32 Particle0Level,
			const int32 Particle1Level,
			FSolverBodyContainer& SolverBodyContainer,
			const FPBDCollisionSolverSettings& SolverSettings)
		{
			// Find the solver bodies for the particles we constrain. This will add them to the container
			// if they aren't there already, and ensure that they are populated with the latest data.
			FSolverBody* Body0 = SolverBodyContainer.FindOrAdd(Constraint->GetParticle0(), Dt);
			FSolverBody* Body1 = SolverBodyContainer.FindOrAdd(Constraint->GetParticle1(), Dt);

			Body0->SetLevel(Particle0Level);
			Body1->SetLevel(Particle1Level);
		
			Solver.SetSolverBodies(*Body0, *Body1);

			// We should try to remove this - the Constraint should not need to know about solver objects
			Constraint->SetSolverBodies(Body0, Body1);

			// Friction values. Static and Dynamic friction are applied in the position solve for most shapes.
			// For quadratic shapes, we run dynamic friction in the velocity solve for better rolling behaviour.
			// We can also run in a mode without static friction at all. This is faster but stacking is not possible.
			// @todo(chaos): fix static/dynamic friction for quadratic shapes
			const FSolverReal StaticFriction = FSolverReal(Constraint->GetStaticFriction());
			const FSolverReal DynamicFriction = FSolverReal(Constraint->GetDynamicFriction());
			FSolverReal PositionStaticFriction = FSolverReal(0);
			FSolverReal PositionDynamicFriction = FSolverReal(0);
			FSolverReal VelocityDynamicFriction = FSolverReal(0);
			if (SolverSettings.NumPositionFrictionIterations > 0)
			{
				PositionStaticFriction = StaticFriction;
				if (!Constraint->HasQuadraticShape())
				{
					PositionDynamicFriction = DynamicFriction;
				}
				else
				{
					VelocityDynamicFriction = DynamicFriction;
				}
			}
			else
			{
				VelocityDynamicFriction = DynamicFriction;
			}

			Solver.SetFriction(PositionStaticFriction, PositionDynamicFriction, VelocityDynamicFriction);

			const FReal SolverStiffness = CalculateSolverStiffness(Body0, Body1, Chaos_PBDCollisionSolver_AutoStiffness_MassRatio1, Chaos_PBDCollisionSolver_AutoStiffness_MassRatio2);
			Solver.SetStiffness(FSolverReal(SolverStiffness));

			Solver.SolverBody0().SetInvMScale(Constraint->GetInvMassScale0());
			Solver.SolverBody0().SetInvIScale(Constraint->GetInvInertiaScale0());
			Solver.SolverBody1().SetInvMScale(Constraint->GetInvMassScale1());
			Solver.SolverBody1().SetInvIScale(Constraint->GetInvInertiaScale1());

			bIsManifold = Constraint->GetUseManifold();
			bIsIncrementalManifold = bIsManifold && Constraint->GetUseIncrementalCollisionDetection();
		}

		void UpdateManifoldPoints(
			const FReal InDt)
		{
			FSolverReal Dt = FSolverReal(InDt);
			const FConstraintSolverBody& Body0 = Solver.SolverBody0();
			const FConstraintSolverBody& Body1 = Solver.SolverBody1();

			// We handle incremental manifolds by just collecting any new contacts
			const int32 BeginPointIndex = bIsIncrementalManifold ? Solver.NumManifoldPoints() : 0;
			const int32 EndPointIndex = Solver.SetNumManifoldPoints(Constraint->GetManifoldPoints().Num());

			const FSolverReal RestitutionVelocityThreshold = FSolverReal(Constraint->GetRestitutionThreshold()) * Dt;
			const FSolverReal Restitution = FSolverReal(Constraint->GetRestitution());

			const FRigidTransform3& ShapeWorldTransform0 = Constraint->GetShapeWorldTransform0();
			const FRigidTransform3& ShapeWorldTransform1 = Constraint->GetShapeWorldTransform1();

			for (int32 ManifoldPointIndex = BeginPointIndex; ManifoldPointIndex < EndPointIndex; ++ManifoldPointIndex)
			{
				TArrayView<FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();
				FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
				if (ManifoldPoint.Flags.bDisabled)
				{
					continue;
				}

				const FVec3 WorldContactPoint0 = ShapeWorldTransform0.TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[0]);
				const FVec3 WorldContactPoint1 = ShapeWorldTransform1.TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[1]);
				const FVec3 WorldContactPoint = FReal(0.5) * (WorldContactPoint0 + WorldContactPoint1);

				const FSolverVec3 WorldContactNormal = FSolverVec3(ShapeWorldTransform1.TransformVectorNoScale(ManifoldPoint.ContactPoint.ShapeContactNormal));
				const FSolverVec3 RelativeContactPosition0 = FSolverVec3(WorldContactPoint - Body0.P());
				const FSolverVec3 RelativeContactPosition1 = FSolverVec3(WorldContactPoint - Body1.P());
				const FSolverReal TargetPhi = FSolverReal(ManifoldPoint.TargetPhi);

				// If we have contact data from a previous tick, use it to calculate the lateral position delta we need
				// to apply to move the contacts back to their original relative locations (i.e., to enforce static friction)
				// @todo(chaos): we should not be writing back to the constraint here - find a better way to update the friction anchor. See FPBDCollisionConstraint::SetSolverResults
				FSolverVec3 WorldFrictionDelta = FSolverVec3(0);
				const FSavedManifoldPoint* SavedManifoldPoint = Constraint->FindSavedManifoldPoint(ManifoldPoint);
				if (SavedManifoldPoint != nullptr)
				{
					const FSolverVec3 FrictionDelta0 = FSolverVec3(SavedManifoldPoint->ShapeContactPoints[0] - ManifoldPoint.ContactPoint.ShapeContactPoints[0]);
					const FSolverVec3 FrictionDelta1 = FSolverVec3(SavedManifoldPoint->ShapeContactPoints[1] - ManifoldPoint.ContactPoint.ShapeContactPoints[1]);
					WorldFrictionDelta = ShapeWorldTransform0.TransformVectorNoScale(FVector(FrictionDelta0)) - ShapeWorldTransform1.TransformVectorNoScale(FVector(FrictionDelta1));

					ManifoldPoint.ShapeAnchorPoints[0] = SavedManifoldPoint->ShapeContactPoints[0];
					ManifoldPoint.ShapeAnchorPoints[1] = SavedManifoldPoint->ShapeContactPoints[1];
				}
				else
				{
					const FSolverVec3 ContactVel0 = Body0.V() + FSolverVec3::CrossProduct(Body0.W(), RelativeContactPosition0);
					const FSolverVec3 ContactVel1 = Body1.V() + FSolverVec3::CrossProduct(Body1.W(), RelativeContactPosition1);
					const FSolverVec3 ContactVel = ContactVel0 - ContactVel1;
					WorldFrictionDelta = ContactVel * Dt;

					ManifoldPoint.ShapeAnchorPoints[0] = ManifoldPoint.ContactPoint.ShapeContactPoints[0];
					ManifoldPoint.ShapeAnchorPoints[1] = ManifoldPoint.ContactPoint.ShapeContactPoints[1];
				}

				// World-space contact tangents. We are treating the normal as the constraint-space Z axis
				// and the Tangent U and V as the constraint-space X and Y axes respectively
				FSolverVec3 WorldContactTangentU = FSolverVec3::CrossProduct(FSolverVec3(0, 1, 0), WorldContactNormal);
				if (!WorldContactTangentU.Normalize(FSolverReal(UE_KINDA_SMALL_NUMBER)))
				{
					WorldContactTangentU = FSolverVec3::CrossProduct(FSolverVec3(1, 0, 0), WorldContactNormal);
					WorldContactTangentU = WorldContactTangentU.GetUnsafeNormal();
				}
				const FSolverVec3 WorldContactTangentV = FSolverVec3::CrossProduct(WorldContactNormal, WorldContactTangentU);

				// The contact point error we are trying to correct in this solver
				const FSolverVec3 WorldContactDelta = FSolverVec3(WorldContactPoint0 - WorldContactPoint1);
				const FSolverReal WorldContactDeltaNormal = FSolverVec3::DotProduct(WorldContactDelta, WorldContactNormal) - TargetPhi;
				const FSolverReal WorldContactDeltaTangentU = FSolverVec3::DotProduct(WorldContactDelta + WorldFrictionDelta, WorldContactTangentU);
				const FSolverReal WorldContactDeltaTangentV = FSolverVec3::DotProduct(WorldContactDelta + WorldFrictionDelta, WorldContactTangentV);

				// Copy all the properties into the solver
				Solver.SetManifoldPoint(
					ManifoldPointIndex,
					Dt,
					Restitution,
					RestitutionVelocityThreshold,
					RelativeContactPosition0,
					RelativeContactPosition1,
					WorldContactNormal,
					WorldContactTangentU,
					WorldContactTangentV,
					WorldContactDeltaNormal,
					WorldContactDeltaTangentU,
					WorldContactDeltaTangentV);

#if CHAOS_DEBUG_DRAW && 0
				if (FDebugDrawQueue::GetInstance().IsDebugDrawingEnabled())
				{
					FDebugDrawQueue::GetInstance().DrawDebugLine(WorldContactPoint1, WorldContactPoint1 - 5.0f * WorldContactDeltaNormal * WorldContactNormal, FColor::Red, false, 0, 10, 0.2f);
					FDebugDrawQueue::GetInstance().DrawDebugLine(WorldContactPoint1, WorldContactPoint1 - 5.0f * WorldContactDeltaTangentU * WorldContactTangentU, FColor::Blue, false, 0, 10, 0.2f);
					FDebugDrawQueue::GetInstance().DrawDebugLine(WorldContactPoint1, WorldContactPoint1 - 5.0f * WorldContactDeltaTangentV * WorldContactTangentV, FColor::Blue, false, 0, 10, 0.2f);
					FDebugDrawQueue::GetInstance().DrawDebugLine(WorldContactPoint0, WorldContactPoint1, FColor::Black, false, 0, 10, 0.2f);
				}
#endif
			}
		}

		/**
		 * @brief Send all solver results to the constraint
		*/
		void ScatterOutput(const FReal Dt)
		{
			FVec3 AccumulatedImpulse = FVec3(0);

			Constraint->ResetSolverResults();

			// NOTE: We only put the non-pruned manifold points into the solver so the ManifoldPointIndex and
			// SolverManifoldPointIndex do not necessarily match. See GatherManifoldPoints
			int32 SolverManifoldPointIndex = 0;
			for (int32 ManifoldPointIndex = 0; ManifoldPointIndex < Constraint->NumManifoldPoints(); ++ManifoldPointIndex)
			{
				FSolverVec3 NetPushOut = FVec3(0);
				FSolverVec3 NetImpulse = FVec3(0);
				FReal StaticFrictionRatio = FReal(0);

				if (!Constraint->GetManifoldPoint(ManifoldPointIndex).Flags.bDisabled)
				{
					const FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = Solver.GetManifoldPoint(SolverManifoldPointIndex);

					NetPushOut = 
						SolverManifoldPoint.NetPushOutNormal * SolverManifoldPoint.WorldContactNormal +
						SolverManifoldPoint.NetPushOutTangentU * SolverManifoldPoint.WorldContactTangentU +
						SolverManifoldPoint.NetPushOutTangentV * SolverManifoldPoint.WorldContactTangentV;

					NetImpulse =
						SolverManifoldPoint.NetImpulseNormal * SolverManifoldPoint.WorldContactNormal +
						SolverManifoldPoint.NetImpulseTangentU * SolverManifoldPoint.WorldContactTangentU +
						SolverManifoldPoint.NetImpulseTangentV * SolverManifoldPoint.WorldContactTangentV;

					StaticFrictionRatio = SolverManifoldPoint.StaticFrictionRatio;

					++SolverManifoldPointIndex;
				}

				// NOTE: We call this even for points we did not run the solver for (but with zero results)
				Constraint->SetSolverResults(ManifoldPointIndex,
					NetPushOut, 
					NetImpulse, 
					StaticFrictionRatio,
					Dt);
			}

			Constraint->SetSolverBodies(nullptr, nullptr);
			Constraint->SetSolverIndex(INDEX_NONE);
			Constraint = nullptr;
			Solver.ResetSolverBodies();
		}


	private:
		FPBDCollisionSolver Solver;
		FPBDCollisionConstraint* Constraint;
		bool bIsManifold;
		bool bIsIncrementalManifold;
	};


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FPBDCollisionSolverContainer::FPBDCollisionSolverContainer()
		: FConstraintSolverContainer()
		, bPerIterationCollisionDetection(false)
		, bDeferredCollisionDetection(false)
	{
	}

	FPBDCollisionSolverContainer::~FPBDCollisionSolverContainer()
	{
	}

	void FPBDCollisionSolverContainer::Reset(const int32 MaxCollisions)
	{
		CollisionSolvers.Reset(MaxCollisions);
	}
	
	void FPBDCollisionSolverContainer::SetNum(const int32 MaxCollisions)
	{
		CollisionSolvers.SetNum(MaxCollisions, false);
	}

	void FPBDCollisionSolverContainer::PreAddConstraintSolver(const FReal Dt, FPBDCollisionConstraint& Constraint, FSolverBodyContainer& SolverBodyContainer, int32& SolverIndex)
	{
		// This container is required to allocate pointers that are valid for the whole tick,
		// so we cannot allow the container to resize during the tick. See Reset()
		check(SolverIndex < CollisionSolvers.Num());
		Constraint.SetSolverIndex(SolverIndex);

		FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];
		CollisionSolver.GetSolver().Reset();

		CollisionSolver.PreGatherInput(Dt, Constraint, SolverBodyContainer);

		++SolverIndex;
	}

	void FPBDCollisionSolverContainer::AddConstraintSolver(const FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FSolverBodyContainer& SolverBodyContainer, const FPBDCollisionSolverSettings& SolverSettings)
	{
		// This container is required to allocate pointers that are valid for the whole tick,
		// so we cannot allow the container to resize during the tick. See Reset()
		const int32 SolverIndex = Constraint.GetSolverIndex();
		check(SolverIndex < CollisionSolvers.Num());

		FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];
		check(CollisionSolver.GetConstraint() == &Constraint);

		CollisionSolver.BindBodies(Dt, Particle0Level, Particle1Level, SolverBodyContainer, SolverSettings);

		if (!bDeferredCollisionDetection)
		{
			CollisionSolver.UpdateManifoldPoints(Dt);
		}

		bPerIterationCollisionDetection |= (!CollisionSolver.IsManifold() || CollisionSolver.IsIncrementalManifold());
	}

	void FPBDCollisionSolverContainer::UpdatePositionShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		// If this is the first shock propagation iteration, enable it on each solver
		const bool bEnableShockPropagation = (It == NumIts - SolverSettings.NumPositionShockPropagationIterations);
		if (bEnableShockPropagation)
		{
			for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().EnablePositionShockPropagation();
			}
		}
	}

	void FPBDCollisionSolverContainer::UpdateVelocityShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		// Set/reset the shock propagation based on current iteration. The position solve may
		// have left the bodies with a mass scale and we want to change or reset it.
		const bool bEnableShockPropagation = (It == NumIts - SolverSettings.NumVelocityShockPropagationIterations);
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

	bool FPBDCollisionSolverContainer::SolvePositionSerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		return SolvePositionImpl(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings, false);
	}

	bool FPBDCollisionSolverContainer::SolveVelocitySerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		return SolveVelocityImpl(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings, false);
	}

	bool FPBDCollisionSolverContainer::SolvePositionParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		return SolvePositionImpl(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings, true);
	}

	bool FPBDCollisionSolverContainer::SolveVelocityParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings)
	{
		return SolveVelocityImpl(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings, true);
	}

	// @todo(chaos): parallel version of SolvePosition
	bool FPBDCollisionSolverContainer::SolvePositionImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);
		if (!bChaos_PBDCollisionSolver_Position_SolveEnabled)
		{
			return false;
		}

		UpdatePositionShockPropagation(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings);

		// Only apply friction for the last few (tunable) iterations
		const bool bApplyStaticFriction = (It >= (NumIts - SolverSettings.NumPositionFrictionIterations));

		// Adjust max pushout to attempt to make it iteration count independent
		const FReal MaxPushOut = (SolverSettings.MaxPushOutVelocity > 0) ? (SolverSettings.MaxPushOutVelocity * Dt) / FReal(NumIts) : 0;

		// We run collision detection here under two conditions (normally it is run after Integration and before the constraint solver phase):
		// 1) When deferring collision detection until the solver phase for better joint-collision behaviour (RBAN). In this case, we only do this on the first iteration.
		// 2) When using no manifolds or incremental manifolds, where we may add/replace manifold points every iteration.
		const bool bDeferredCollisions = bDeferredCollisionDetection && (It == 0);
		if (bDeferredCollisions || bPerIterationCollisionDetection)
		{
			UpdateCollisions(Dt, BeginIndex, EndIndex);
		}

		// Apply the position correction
		if (bApplyStaticFriction)
		{
			return SolvePositionWithFrictionImpl(Dt, BeginIndex, EndIndex, MaxPushOut, bParallel);
		}
		else
		{
			return SolvePositionNoFrictionImpl(Dt, BeginIndex, EndIndex, MaxPushOut, bParallel);
		}
	}

	// Solve position with friction (last few iterations each tick)
	bool FPBDCollisionSolverContainer::SolvePositionWithFrictionImpl(const FReal InDt, const int32 BeginIndex, const int32 EndIndex, const FReal InMaxPushOut, const bool bParallel)
	{
		if (EndIndex == BeginIndex)
		{
			return false;
		}
		const FSolverReal Dt = FSolverReal(InDt);
		const FSolverReal MaxPushOut = FSolverReal(InMaxPushOut);

		InnerPhysicsParallelForRange(EndIndex - BeginIndex, [&](int32 StartRangeIndex, int32 EndRangeIndex)
		{
			int32 LocalStartIndex = StartRangeIndex + BeginIndex;
			int32 LocalEndIndex = EndRangeIndex + BeginIndex;
			for (int32 SolverIndex = LocalStartIndex; SolverIndex < LocalEndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().SolvePositionWithFriction(Dt, MaxPushOut);
			}
		}, Chaos::LargeBatchSize, !bParallel);
		return true;
	}

	// Solve position without friction (first few iterations each tick)
	bool FPBDCollisionSolverContainer::SolvePositionNoFrictionImpl(const FReal InDt, const int32 BeginIndex, const int32 EndIndex, const FReal InMaxPushOut, const bool bParallel)
	{
		if (EndIndex == BeginIndex)
		{
			return false;
		}
		const FSolverReal Dt = FSolverReal(InDt);
		const FSolverReal MaxPushOut = FSolverReal(InMaxPushOut);

		InnerPhysicsParallelForRange(EndIndex - BeginIndex, [&](int32 StartRangeIndex, int32 EndRangeIndex)
		{
			int32 LocalStartIndex = StartRangeIndex + BeginIndex;
			int32 LocalEndIndex = EndRangeIndex + BeginIndex;
			for (int32 SolverIndex = LocalStartIndex; SolverIndex < LocalEndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].GetSolver().SolvePositionNoFriction(Dt, MaxPushOut);
			}
		}, Chaos::LargeBatchSize, !bParallel);
		return true;
	}

	bool FPBDCollisionSolverContainer::SolveVelocityImpl(const FReal InDt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings, const bool bParallel)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);
		if (!bChaos_PBDCollisionSolver_Velocity_SolveEnabled)
		{
			return false;
		}
		const FSolverReal Dt = FSolverReal(InDt);

		UpdateVelocityShockPropagation(Dt, It, NumIts, BeginIndex, EndIndex, SolverSettings);

		const bool bApplyDynamicFriction = (It >= NumIts - SolverSettings.NumVelocityFrictionIterations);

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
		if (EndIndex == BeginIndex)
		{
			return;
		}

		InnerPhysicsParallelForRange(EndIndex - BeginIndex, [&](int32 LocalStartIndex, int32 LocalEndIndex)
		{
			int32 LoopStartIndex = LocalStartIndex + BeginIndex;
			int32 LoopEndIndex = LocalEndIndex + BeginIndex;
			for (int32 SolverIndex = LoopStartIndex; SolverIndex < LoopEndIndex; ++SolverIndex)
			{
				CollisionSolvers[SolverIndex].ScatterOutput(Dt);
			}
		}, Chaos::LargeBatchSize);
	}

	void FPBDCollisionSolverContainer::UpdateCollisions(const FReal InDt, const int32 BeginIndex, const int32 EndIndex)
	{
		const FSolverReal Dt = FSolverReal(InDt);

		bool bNeedsAnotherIteration = false;
		for (int32 SolverIndex = BeginIndex; SolverIndex < EndIndex; ++SolverIndex)
		{
			FPBDCollisionSolverAdapter& CollisionSolver = CollisionSolvers[SolverIndex];
			if (!CollisionSolver.IsManifold() || CollisionSolver.IsIncrementalManifold() || bDeferredCollisionDetection)
			{
				FPBDCollisionConstraint* Constraint = CollisionSolver.GetConstraint();

				// Run collision detection at the current transforms including any correction from previous iterations
				const FSolverBody& Body0 = CollisionSolver.GetSolver().SolverBody0().SolverBody();
				const FSolverBody& Body1 = CollisionSolver.GetSolver().SolverBody1().SolverBody();
				const FRigidTransform3 CorrectedActorWorldTransform0 = FRigidTransform3(Body0.CorrectedActorP(), Body0.CorrectedActorQ());
				const FRigidTransform3 CorrectedActorWorldTransform1 = FRigidTransform3(Body1.CorrectedActorP(), Body1.CorrectedActorQ());
				const FRigidTransform3 CorrectedShapeWorldTransform0 = Constraint->GetShapeRelativeTransform0() * CorrectedActorWorldTransform0;
				const FRigidTransform3 CorrectedShapeWorldTransform1 = Constraint->GetShapeRelativeTransform1() * CorrectedActorWorldTransform1;

				// NOTE: We deliberately have not updated the ShapwWorldTranforms on the constraint. If we did that, we would calculate 
				// errors incorrectly in UpdateManifoldPoints, because the solver assumes nothing has been moved as we iterate (we accumulate 
				// corrections that will be applied later.)
				Constraint->ResetPhi(Constraint->GetCullDistance());
				Collisions::UpdateConstraint(*Constraint, CorrectedShapeWorldTransform0, CorrectedShapeWorldTransform1, Dt);

				// Update the manifold based on the new or updated contacts
				CollisionSolver.UpdateManifoldPoints(Dt);
			}
		}
	}

}
