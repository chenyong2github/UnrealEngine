// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"

namespace Chaos
{
	class FSolverBody;
	class FSolverBodyContainer;

	// A pair of pointers so solver bodies
	// @note Pointers are only valid for the Constraint Solving phase of the tick
	using FSolverBodyPtrPair = TVector<FSolverBody*, 2>;

	/**
	 * Used by the constraint solver loop to cache all state for a particle and accumulate solver results.
	 * Uses a gather/scatter mechanism to read/write data to the particle SOAs at the beginning/end of the constraint solve.
	 * Constraint solver algorithms, and collision Update functions are implemented to use FSolverBody, and do not 
	 * directly read/write to the particle handles. Constraint Solvers will modify P(), Q(), V() and W() via 
	 * ApplyTransformDelta() and other methods.
	 * 
	 * There is one SolverBody for each particle in an island. Most constraint solvers will actually wrap the
	 * FSolverBody in FConstraintSolverBody, which allows us to apply per-constraint modifiers to the Solver Body.
	 *
	 * @note the X(), P(), R(), Q() accessors on FSolverBody return the Center of Mass positions and rotations, in contrast
	 * to the Particle methods which gives Actor positions and rotations. This is because the Constraint Solvers all calculate
	 * impulses and position corrections relative to the center of mass.
	 * 
	 * @todo(chaos): layout for cache
	 */
	class FSolverBody
	{
	public:

		/**
		 * @brief Create an empty solver body
		 * @note This is only used by unit tests
		*/
		FSolverBody();

		/**
		 * @brief Calculate and set the velocity and angular velocity from the net transform delta
		*/
		void SetImplicitVelocity(FReal Dt)
		{
			if (IsDynamic())
			{
				State.V = FVec3::CalculateVelocity(State.X, State.P, Dt);
				State.W = FRotation3::CalculateAngularVelocity(State.R, State.Q, Dt);
			}
		}

		/**
		 * @brief Get the inverse mass
		*/
		FReal InvM() const { return State.InvM; }

		/**
		 * @brief Set the inverse mass
		*/
		void SetInvM(FReal InInvM) { State.InvM = InInvM; }

		/**
		 * @brief Get the world-space inverse inertia
		*/
		const FMatrix33& InvI() const { return State.InvI; }

		/**
		 * @brief Set the world-space inverse inertia
		*/
		void SetInvI(const FMatrix33& InInvI) { State.InvI = InInvI; }

		/**
		 * @brief Get the local-space inverse inertia (diagonal elements)
		*/
		const FVec3& InvILocal() const { return State.InvILocal; }

		/**
		 * @brief Set the local-space inverse inertia (diagonal elements)
		*/
		void SetInvILocal(const FVec3& InInvILocal)
		{ 
			State.InvILocal = InInvILocal; 
			UpdateRotationDependentState();
		}

		/**
		 * @brief The current CoM transform
		*/
		FRigidTransform3 CoMTransform() const { return FRigidTransform3(P(), Q()); }

		/**
		 * @brief Pre-integration world-space center of mass position
		*/
		const FVec3& X() const { return State.X; }
		void SetX(const FVec3& InX) { State.X = InX; }

		/**
		 * @brief Pre-integration world-space center of mass rotation
		*/
		const FRotation3& R() const { return State.R; }
		void SetR(const FRotation3& InR) { State.R = InR; }

		/**
		 * @brief World-space center of mass position
		*/
		const FVec3& P() const { return State.P; }
		void SetP(const FVec3& InP) { State.P = InP; }

		/**
		 * @brief World-space center of mass rotation
		*/
		const FRotation3& Q() const { return State.Q; }
		void SetQ(const FRotation3& InQ) { State.Q = InQ; }

		/**
		 * @brief World-space center of mass velocity
		*/
		const FVec3& V() const { return State.V; }
		void SetV(const FVec3& InV) { State.V = InV; }

		/**
		 * @brief World-space center of mass angular velocity
		*/
		const FVec3& W() const { return State.W; }
		void SetW(const FVec3& InW) { State.W = InW; }

		const FVec3& CoM() const { return State.CoM; }
		void SetCoM(const FVec3& InCoM) { State.CoM = InCoM; }

		const FRotation3& RoM() const { return State.RoM; }
		void SetRoM(const FRotation3& InRoM) { State.RoM = InRoM; }

		/**
		 * @brief Get the current world-space Actor position 
		 * @note This is recalculated from the current CoM transform.
		*/
		FVec3 ActorP() const { return P() - ActorQ().RotateVector(CoM()); }

		/**
		 * @brief Get the current world-space Actor rotation
		 * @note This is recalculated from the current CoM transform.
		*/
		FRotation3 ActorQ() const { return Q() * RoM().Inverse(); }

		/**
		 * @brief Contact graph level. This is used in shock propagation to determine which of two bodies should have its inverse mass scaled
		*/
		int32 Level() const { return State.Level; }
		void SetLevel(int32 InLevel) { State.Level = InLevel; }

		/**
		 * @brief Whether there were any active collision constraints on this body
		*/
		bool HasActiveCollision() const { return State.bHasActiveCollision; }
		void SetHasActiveCollision(bool bInHasCollision) { State.bHasActiveCollision = bInHasCollision; }

		/**
		 * @brief Whether the body has a finite mass
		 * @note This is based on the current inverse mass, so a "dynamic" particle with 0 inverse mass will return true here.
		*/
		bool IsDynamic() const { return (State.InvM > SMALL_NUMBER); }

		/**
		 * @brief Apply a world-space position and rotation delta to the body center of mass, and update inverse mass
		*/
		void ApplyTransformDelta(const FVec3 DP, const FVec3 DR)
		{
			ApplyPositionDelta(DP);
			ApplyRotationDelta(DR);
		}

		/**
		 * @brief Apply a world-space position delta to the solver body center of mass
		*/
		void ApplyPositionDelta(const FVec3 DP)
		{
			State.P += DP;
			++State.LastChangeEpoch;
		}

		/**
		 * @brief Apply a world-space rotation delta to the solver body and update the inverse mass
		*/
		void ApplyRotationDelta(const FVec3 DR)
		{
			State.Q += FRotation3::FromElements(DR, 0.f) * State.Q * FReal(0.5);
			State.Q.Normalize();
			++State.LastChangeEpoch;

			//if (bUpdateRotationDependentState)
			//{
			//	UpdateRotationDependentState();
			//}
		}

		/**
		 * @brief Apply a world-space velocity delta to the solver body
		*/
		void ApplyVelocityDelta(const FVec3& DV, const FVec3& DW)
		{
			ApplyLinearVelocityDelta(DV);
			ApplyAngularVelocityDelta(DW);
		}

		/**
		 * @brief Apply a world-space linear velocity delta to the solver body
		*/
		void ApplyLinearVelocityDelta(const FVec3& DV)
		{
			State.V += DV;
			++State.LastChangeEpoch;
		}

		/**
		 * @brief Apply an world-space angular velocity delta to the solver body
		*/
		void ApplyAngularVelocityDelta(const FVec3& DW)
		{
			State.W += DW;
			++State.LastChangeEpoch;
		}

		/**
		 * @brief Update the rotation to be in the same hemisphere as the provided quaternion.
		 * This is used by joints with angular constraint/drives
		*/
		void EnforceShortestRotationTo(const FRotation3& InQ)
		{
			State.Q.EnforceShortestArcWith(InQ);
		}

		int32 LastChangeEpoch() const
		{
			return State.LastChangeEpoch;
		}

		/**
		 * @brief Update cached state that depends on rotation (i.e., world space inertia)
		*/
		void UpdateRotationDependentState();

	private:

		// The struct exists only so that we can use the variable names
		// as accessor names without violation the variable naming convention
		struct FState
		{
			FState()
				: InvILocal(0)
				, InvM(0)
				, InvI(0)
				, X(0)
				, R(FRotation3::Identity)
				, P(0)
				, Q(FRotation3::Identity)
				, V(0)
				, W(0)
				, CoM(0)
				, RoM(FRotation3::Identity)
				, Level(0)
				, LastChangeEpoch(0)
				, bHasActiveCollision(false)
			{}

			// Local-space inverse inertia (diagonal, so only 3 elements)
			FVec3 InvILocal;

			// Inverse mass
			FReal InvM;

			// World-space inverse inertia
			// @todo(chaos): do we need this, or should we force all systems to use the FConstraintSolverBody decorator?
			FMatrix33 InvI;

			// World-space center of mass state at start of sub step
			FVec3 X;

			// World-space rotation of mass
			FRotation3 R;

			// World-space center of mass position
			FVec3 P;

			// World-space center of mass rotation
			FRotation3 Q;

			// World-space center of mass velocity
			FVec3 V;

			// World-space center of mass angular velocity
			FVec3 W;

			// Actor-space center of mass location
			FVec3 CoM;

			// Actor-space center of mass rotation
			FRotation3 RoM;

			// Distance to a kinmatic body (through the contact graph). Used by collision shock propagation
			int32 Level;

			// A counter incremented every time the state changes. 
			// Used by constraints to determine if some other constraint has modified the body for early exit logic
			int32 LastChangeEpoch;

			// Whether we had any active contacts this substep
			// @todo(chaos): maybe make this a counter?
			bool bHasActiveCollision;
		};

		//FGenericParticleHandle Particle;
		FState State;
	};

	/**
	 * An FSolverBody decorator for adding mass modifiers to a SolverBody. This will scale the
	 * inverse mass and inverse inertia using the supplied scale. It also updates IsDynamic() to
	 * return false if the scaled inverse mass is zero.
	 * 
	 * See FSolverBody for comments on methods.
	 * 
	 * @note This functionality cannot be in FSolverBody because two constraints referencing
	 * the same body may be applying different mass modifiers (e.g., Joints support "bParentDominates"
	 * which is a per-constraint property, not a per-body property.
	 */
	class FConstraintSolverBody
	{
	public:
		FConstraintSolverBody()
			: Body(nullptr)
		{
		}

		FConstraintSolverBody(FSolverBody& InBody)
			: Body(&InBody)
		{
		}

		FConstraintSolverBody(FSolverBody& InBody, FReal InInvMassScale)
			: Body(&InBody)
		{
			SetInvMassScale(InInvMassScale);
		}

		/**
		 * @brief True if we have been set up to decorate a SolverBody
		*/
		bool IsValid() const { return Body != nullptr; }

		/**
		 * @brief Invalidate the solver body reference
		*/
		void Reset() { Body = nullptr; }

		/**
		 * @brief The decorated SolverBody
		*/
		FSolverBody& SolverBody() { check(IsValid()); return *Body; }
		const FSolverBody& SolverBody() const { check(IsValid()); return *Body; }

		/**
		 * @brief A scale applied to both inverse mass and inverse inertia
		*/
		FReal InvMassScale() const { return State.InvMassScale; }
		void SetInvMassScale(FReal InInvMassScale) { State.InvMassScale = InInvMassScale; }

		/**
		 * @brief The scaled inverse mass
		*/
		FReal InvM() const { return State.InvMassScale * Body->InvM(); }

		/**
		 * @brief The scaled inverse inertia
		*/
		FMatrix33 InvI() const { return State.InvMassScale * Body->InvI(); }

		/**
		 * @brief Whether the body is dynamic (i.e., has a finite mass) after InvMassScale is applied
		*/
		bool IsDynamic() const { return (InvM() > SMALL_NUMBER); }

		//
		// From here all methods just forward to the FSolverBody
		//

		void SetImplicitVelocity(FReal Dt) { Body->SetImplicitVelocity(Dt); }
		FRigidTransform3 CoMTransform() const { return Body->CoMTransform(); }
		const FVec3& X() const { return Body->X(); }
		const FRotation3& R() const { return Body->R(); }
		const FVec3& P() const { return Body->P(); }
		const FRotation3& Q() const { return Body->Q(); }
		const FVec3 ActorP() const { return Body->ActorP(); }
		const FRotation3 ActorQ() const { return Body->ActorQ(); }
		const FVec3& V() const { return Body->V(); }
		const FVec3& W() const { return Body->W(); }
		int32 Level() const { return Body->Level(); }

		void ApplyTransformDelta(const FVec3 DP, const FVec3 DR) { Body->ApplyTransformDelta(DP, DR); }
		void ApplyPositionDelta(const FVec3 DP) { Body->ApplyPositionDelta(DP); }
		void ApplyRotationDelta(const FVec3 DR) { Body->ApplyRotationDelta(DR); }
		void ApplyVelocityDelta(const FVec3& DV, const FVec3& DW) { Body->ApplyVelocityDelta(DV, DW); }
		void ApplyLinearVelocityDelta(const FVec3& DV) { Body->ApplyLinearVelocityDelta(DV); }
		void ApplyAngularVelocityDelta(const FVec3& DW) { Body->ApplyAngularVelocityDelta(DW); }
		void EnforceShortestRotationTo(const FRotation3& InQ) { Body->EnforceShortestRotationTo(InQ); }
		void UpdateRotationDependentState() { Body->UpdateRotationDependentState(); }

		int32 LastChangeEpoch() const { return Body->LastChangeEpoch(); }

	private:
		// Struct is only so that we can use the same var names as function names
		struct FState
		{
			FState() 
				: InvMassScale(FReal(1)) 
			{}
			FReal InvMassScale;
		};

		// The body we decorate
		FSolverBody* Body;

		// The body modifiers
		FState State;
	};
}
