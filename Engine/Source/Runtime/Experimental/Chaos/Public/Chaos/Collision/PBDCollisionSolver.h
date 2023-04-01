// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBody.h"

namespace Chaos
{
	class FManifoldPoint;
	class FPBDCollisionConstraint;

	namespace Private
	{
		class FPBDCollisionSolver;
	}

	namespace CVars
	{
		extern bool bChaos_PBDCollisionSolver_Velocity_AveragePointEnabled;
		extern bool bChaos_PBDCollisionSolver_Velocity_FrictionEnabled;
		extern float Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
	}

	namespace Private
	{
		/**
		 * @brief A single contact point in a FPBDCollisionSolver
		 * @note Internal Chaos class subject to API changes, renaming or removal
		*/
		class FPBDCollisionSolverManifoldPoint
		{
		public:
			// World-space contact point relative to each particle's center of mass
			FVec3f RelativeContactPoints[2];

			// World-space contact normal and tangents
			FVec3f ContactNormal;
			FVec3f ContactTangentU;
			FVec3f ContactTangentV;

			// Errors to correct along each of the contact axes
			FRealSingle ContactDeltaNormal;
			FRealSingle ContactDeltaTangentU;
			FRealSingle ContactDeltaTangentV;

			// Target velocity along the normal direction
			FRealSingle ContactTargetVelocityNormal;

			// I^-1.(R x A) for each body where A is each axis (Normal, TangentU, TangentV)
			FSolverVec3 ContactNormalAngular0;
			FSolverVec3 ContactTangentUAngular0;
			FSolverVec3 ContactTangentVAngular0;
			FSolverVec3 ContactNormalAngular1;
			FSolverVec3 ContactTangentUAngular1;
			FSolverVec3 ContactTangentVAngular1;

			// Contact mass (for non-friction)
			FSolverReal ContactMassNormal;
			FSolverReal ContactMassTangentU;
			FSolverReal ContactMassTangentV;

			// Solver outputs
			FSolverReal NetPushOutNormal;
			FSolverReal NetPushOutTangentU;
			FSolverReal NetPushOutTangentV;
			FSolverReal NetImpulseNormal;
			FSolverReal NetImpulseTangentU;
			FSolverReal NetImpulseTangentV;

			// A measure of how much we exceeded the static friction threshold.
			// Equal to (NormalPushOut / TangentialPushOut) before clamping to the friction cone.
			// Used to move the static friction anchors to the edge of the cone in Scatter.
			FSolverReal StaticFrictionRatio;
		};

		/**
		 * @brief
		 * @todo(chaos): Make this solver operate on a single contact point rather than all points in a manifold.
		 * This would be beneficial if we have many contacts with less than 4 points in the manifold. However this
		 * is dificult to do while we are still supporting non-manifold collisions.
		*/
		class FPBDCollisionSolver
		{
		public:
			static const int32 MaxConstrainedBodies = 2;
			static const int32 MaxPointsPerConstraint = 4;

			// Create a solver that is initialized to safe defaults
			static FPBDCollisionSolver MakeInitialized()
			{
				FPBDCollisionSolver Solver;
				Solver.State.Init();
				return Solver;
			}

			// Create a solver with no initialization
			static FPBDCollisionSolver MakeUninitialized()
			{
				return FPBDCollisionSolver();
			}

			// NOTE: Does not initialize any properties. See MakeInitialized
			FPBDCollisionSolver() {}

			/** Reset the state of the collision solver */
			void Reset()
			{
				State.SolverBodies[0].Reset();
				State.SolverBodies[1].Reset();
				State.ManifoldPoints = nullptr;
				State.NumManifoldPoints = 0;
				State.MaxManifoldPoints = 0;
			}

			void ResetManifold()
			{
				State.NumManifoldPoints = 0;
			}

			FSolverReal GetStaticFriction() const { return State.StaticFriction; }
			FSolverReal GetDynamicFriction() const { return State.DynamicFriction; }
			FSolverReal GetVelocityFriction() const { return State.VelocityFriction; }

			void SetFriction(const FSolverReal InStaticFriction, const FSolverReal InDynamicFriction, const FSolverReal InVelocityFriction)
			{
				State.StaticFriction = InStaticFriction;
				State.DynamicFriction = InDynamicFriction;
				State.VelocityFriction = InVelocityFriction;
			}

			void SetStiffness(const FSolverReal InStiffness)
			{
				State.Stiffness = InStiffness;
			}

			void SetSolverBodies(FSolverBody& SolverBody0, FSolverBody& SolverBody1)
			{
				State.SolverBodies[0].SetSolverBody(SolverBody0);
				State.SolverBodies[1].SetSolverBody(SolverBody1);
			}

			int32 NumManifoldPoints() const
			{
				return State.NumManifoldPoints;
			}

			int32 MaxManifoldPoints() const
			{
				return State.MaxManifoldPoints;
			}

			int32 AddManifoldPoint()
			{
				check(State.NumManifoldPoints < State.MaxManifoldPoints);
				return State.NumManifoldPoints++;
			}

			void SetManifoldPointsBuffer(FPBDCollisionSolverManifoldPoint* InManifoldPoints, const int32 InMaxManifoldPoints)
			{
				State.ManifoldPoints = InManifoldPoints;
				State.MaxManifoldPoints = InMaxManifoldPoints;
				State.NumManifoldPoints = 0;
			}

			const FPBDCollisionSolverManifoldPoint& GetManifoldPoint(const int32 ManifoldPointIndex) const
			{
				check(ManifoldPointIndex < NumManifoldPoints());
				return State.ManifoldPoints[ManifoldPointIndex];
			}

			/**
			 * Set up a manifold point (except mass properties. @see UpdateMass())
			*/
			void InitManifoldPoint(
				const int32 PointIndex,
				const FSolverReal Dt,
				const FSolverVec3& InRelativeContactPosition0,
				const FSolverVec3& InRelativeContactPosition1,
				const FSolverVec3& InWorldContactNormal,
				const FSolverVec3& InWorldContactTangentU,
				const FSolverVec3& InWorldContactTangentV,
				const FSolverReal InWorldContactDeltaNormal,
				const FSolverReal InWorldContactDeltaTangentU,
				const FSolverReal InWorldContactDeltaTangentV,
				const FSolverReal InWorldContactVelocityTargetNormal);

			/**
			 * Call once all manifold points have been initialized. Calculate mass properties etc.
			 */
			void FinalizeManifold();

			/**
			 * @brief Get the first solver body
			 * NOTE: This will not include any shock propagation mass scaling
			*/
			FConstraintSolverBody& SolverBody0() { return State.SolverBodies[0]; }
			const FConstraintSolverBody& SolverBody0() const { return State.SolverBodies[0]; }


			/**
			 * @brief Get the second solver body
			 * NOTE: This will not include any shock propagation mass scaling
			*/
			FConstraintSolverBody& SolverBody1() { return State.SolverBodies[1]; }
			const FConstraintSolverBody& SolverBody1() const { return State.SolverBodies[1]; }


			/**
			 * @brief Calculate and apply the position correction for this iteration
			 * @return true if we need to run more iterations, false if we did not apply any correction
			*/
			void SolvePositionNoFriction(
				const FSolverReal Dt,
				const FSolverReal MaxPushOut);

			void SolvePositionWithFriction(
				const FSolverReal Dt, 
				const FSolverReal MaxPushOut);

			/**
			 * @brief Calculate and apply the velocity correction for this iteration
			 * @return true if we need to run more iterations, false if we did not apply any correction
			*/
			void SolveVelocity(
				const FSolverReal Dt, 
				const bool bApplyDynamicFriction);

			void UpdateMass();

			void UpdateMassNormal();

		private:
			// Get the mass properties for the bodies if they are dynamic
			FORCEINLINE_DEBUGGABLE void GetDynamicMassProperties(
				FSolverReal& OutInvM0,
				FSolverMatrix33& OutInvI0, 
				FSolverReal& OutInvM1, 
				FSolverMatrix33& OutInvI1)
			{
				OutInvM0 = 0;
				OutInvM1 = 0;

				FConstraintSolverBody& Body0 = SolverBody0();
				if (Body0.IsDynamic())
				{
					OutInvM0 = Body0.InvM();
					OutInvI0 = Body0.InvI();
				}

				FConstraintSolverBody& Body1 = SolverBody1();
				if (Body1.IsDynamic())
				{
					OutInvM1 = Body1.InvM();
					OutInvI1 = Body1.InvI();
				}
			}

			bool IsDynamic(const int32 BodyIndex) const
			{
				return (State.InvMs[BodyIndex] > FSolverBody::ZeroMassThreshold());
			}

			void CalculateContactPositionErrorNormal(
				const FPBDCollisionSolverManifoldPoint& ManifoldPoint, 
				const FSolverReal MaxPushOut, 
				FSolverReal& OutContactDeltaNormal) const;

			void CalculateContactPositionErrorTangential(
				const FPBDCollisionSolverManifoldPoint& ManifoldPoint, 
				FSolverReal& OutContactDeltaTangentU, 
				FSolverReal& OutContactDeltaTangentV) const;

			void CalculateContactVelocityError(
				const FPBDCollisionSolverManifoldPoint& ManifoldPoint, 
				const FSolverReal DynamicFriction, 
				const FSolverReal Dt, 
				FSolverReal& OutContactVelocityDeltaNormal, 
				FSolverReal& OutContactVelocityDeltaTangent0, 
				FSolverReal& OutContactVelocityDeltaTangent1) const;

			void CalculateContactVelocityErrorNormal(
				const FPBDCollisionSolverManifoldPoint& ManifoldPoint, 
				FSolverReal& OutContactVelocityDeltaNormal) const;

			bool ShouldSolveVelocity(
				const FPBDCollisionSolverManifoldPoint& ManifoldPoint) const;

			void CalculatePositionCorrectionNormal(
				const FSolverReal Stiffness,
				const FSolverReal ContactDeltaNormal,
				const FSolverReal ContactMassNormal,
				const FSolverReal NetPushOutNormal,
				FSolverReal& OutPushOutNormal);

			void CalculatePositionCorrectionTangent(
				const FSolverReal Stiffness,
				const FSolverReal ContactDeltaTangent,
				const FSolverReal ContactMassTangent,
				const FSolverReal NetPushOutTangent,
				FSolverReal& OutPushOutTangent);

			void ApplyFrictionCone(
				const FSolverReal StaticFriction,
				const FSolverReal DynamicFriction,
				const FSolverReal MaxFrictionPushOut,
				FSolverReal& InOutPushOutTangentU,
				FSolverReal& InOutPushOutTangentV,
				FSolverReal& InOutNetPushOutTangentU,
				FSolverReal& InOutNetPushOutTangentV,
				FSolverReal& OutStaticFrictionRatio);

			void ApplyPositionCorrectionTangential(
				const FSolverReal Stiffness,
				const FSolverReal StaticFriction,
				const FSolverReal DynamicFriction,
				const FSolverReal MaxFrictionPushOut,
				const FSolverReal ContactDeltaTangentU,
				const FSolverReal ContactDeltaTangentV,
				FPBDCollisionSolverManifoldPoint& ManifoldPoint);

			void ApplyPositionCorrectionNormal(
				const FSolverReal Stiffness,
				const FSolverReal ContactDeltaNormal,
				FPBDCollisionSolverManifoldPoint& ManifoldPoint);

			void ApplyVelocityCorrection(
				const FSolverReal Stiffness,
				const FSolverReal Dt,
				const FSolverReal DynamicFriction,
				const FSolverReal ContactVelocityDeltaNormal,
				const FSolverReal ContactVelocityDeltaTangent0,
				const FSolverReal ContactVelocityDeltaTangent1,
				const FSolverReal MinImpulseNormal,
				FPBDCollisionSolverManifoldPoint& ManifoldPoint);

			void ApplyVelocityCorrectionNormal(
				const FSolverReal Stiffness,
				const FSolverReal ContactVelocityDeltaNormal,
				const FSolverReal MinImpulseNormal,
				FPBDCollisionSolverManifoldPoint& ManifoldPoint);

			/**
			 * @brief Run a velocity solve on the average point from all the points that received a position impulse
			 * This is used to enforce Restitution constraints without introducing rotation artefacts without
			 * adding more velocity iterations.
			 * This will only perform work if there is more than one active contact.
			*/
			void SolveVelocityAverage(const FSolverReal Dt);

			struct FState
			{
				FState()
				{
				}

				void Init()
				{
					StaticFriction = 0;
					DynamicFriction = 0;
					VelocityFriction = 0;
					Stiffness = 1;
					SolverBodies[0].Reset();
					SolverBodies[1].Reset();
					ManifoldPoints = nullptr;
					NumManifoldPoints = 0;
					MaxManifoldPoints = 0;
				}

				// Static Friction in the position-solve phase
				FSolverReal StaticFriction;

				// Dynamic Friction in the position-solve phase
				FSolverReal DynamicFriction;

				// Dynamic Friction in the velocity-solve phase
				FSolverReal VelocityFriction;

				// Solver stiffness (scales all pushout and impulses)
				FSolverReal Stiffness;

				// Bodies
				FConstraintSolverBody SolverBodies[MaxConstrainedBodies];
				FSolverReal InvMs[2];

				// Manifold Points
				FPBDCollisionSolverManifoldPoint* ManifoldPoints;
				int32 NumManifoldPoints;
				int32 MaxManifoldPoints;
			};

			FState State;
		};


		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculatePositionCorrectionNormal(
			const FSolverReal Stiffness,
			const FSolverReal ContactDeltaNormal,
			const FSolverReal ContactMassNormal,
			const FSolverReal NetPushOutNormal,
			FSolverReal& OutPushOutNormal)
		{
			const FSolverReal PushOutNormal = -Stiffness * ContactDeltaNormal * ContactMassNormal;

			// The total pushout so far this sub-step
			// Unilateral constraint: Net-negative impulses not allowed (negative incremental impulses are allowed as long as the net is positive)
			if ((NetPushOutNormal + PushOutNormal) > FSolverReal(0))
			{
				OutPushOutNormal = PushOutNormal;
			}
			else
			{
				OutPushOutNormal = -NetPushOutNormal;
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculatePositionCorrectionTangent(
			const FSolverReal Stiffness,
			const FSolverReal ContactDeltaTangent,
			const FSolverReal ContactMassTangent,
			const FSolverReal NetPushOutTangent,
			FSolverReal& OutPushOutTangent)
		{
			// Bilateral constraint - negative values allowed (unlike the normal correction)
			OutPushOutTangent = -Stiffness * ContactMassTangent * ContactDeltaTangent;
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::ApplyFrictionCone(
			const FSolverReal StaticFriction,
			const FSolverReal DynamicFriction,
			const FSolverReal MaxFrictionPushOut,
			FSolverReal& InOutPushOutTangentU,
			FSolverReal& InOutPushOutTangentV,
			FSolverReal& InOutNetPushOutTangentU,
			FSolverReal& InOutNetPushOutTangentV,
			FSolverReal& OutStaticFrictionRatio)
		{
			FSolverReal ClampedPushOutTangentU = InOutPushOutTangentU;
			FSolverReal ClampedPushOutTangentV = InOutPushOutTangentV;
			FSolverReal ClampedNetPushOutTangentU = InOutNetPushOutTangentU + InOutPushOutTangentU;
			FSolverReal ClampedNetPushOutTangentV = InOutNetPushOutTangentV + InOutPushOutTangentV;
			FSolverReal ClampedStaticFrictionRatio = FSolverReal(1);

			if (MaxFrictionPushOut < FSolverReal(UE_KINDA_SMALL_NUMBER))
			{
				ClampedPushOutTangentU = -InOutNetPushOutTangentU;
				ClampedPushOutTangentV = -InOutNetPushOutTangentV;
				ClampedNetPushOutTangentU = FSolverReal(0);
				ClampedNetPushOutTangentV = FSolverReal(0);
				ClampedStaticFrictionRatio = FSolverReal(0);
			}
			else
			{
				// If we exceed the static friction cone, clip to the dynamic friction cone
				const FSolverReal MaxStaticPushOutTangentSq = FMath::Square(StaticFriction * MaxFrictionPushOut);
				const FSolverReal NetPushOutTangentSq = FMath::Square(ClampedNetPushOutTangentU) + FMath::Square(ClampedNetPushOutTangentV);
				if (NetPushOutTangentSq > MaxStaticPushOutTangentSq)
				{
					const FSolverReal MaxDynamicPushOutTangent = DynamicFriction * MaxFrictionPushOut;
					const FSolverReal FrictionMultiplier = MaxDynamicPushOutTangent * FMath::InvSqrt(NetPushOutTangentSq);
					ClampedNetPushOutTangentU = FrictionMultiplier * ClampedNetPushOutTangentU;
					ClampedNetPushOutTangentV = FrictionMultiplier * ClampedNetPushOutTangentV;
					ClampedPushOutTangentU = ClampedNetPushOutTangentU - InOutNetPushOutTangentU;
					ClampedPushOutTangentV = ClampedNetPushOutTangentV - InOutNetPushOutTangentV;
					ClampedStaticFrictionRatio = FrictionMultiplier;
				}
			}

			InOutPushOutTangentU = ClampedPushOutTangentU;
			InOutPushOutTangentV = ClampedPushOutTangentV;
			InOutNetPushOutTangentU = ClampedNetPushOutTangentU;
			InOutNetPushOutTangentV = ClampedNetPushOutTangentV;
			OutStaticFrictionRatio = ClampedStaticFrictionRatio;
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::ApplyPositionCorrectionTangential(
			const FSolverReal Stiffness,
			const FSolverReal StaticFriction,
			const FSolverReal DynamicFriction,
			const FSolverReal MaxFrictionPushOut,
			const FSolverReal ContactDeltaTangentU,
			const FSolverReal ContactDeltaTangentV,
			FPBDCollisionSolverManifoldPoint& ManifoldPoint)
		{
			FSolverReal PushOutTangentU = FSolverReal(0);
			FSolverReal PushOutTangentV = FSolverReal(0);

			CalculatePositionCorrectionTangent(
				Stiffness,
				ContactDeltaTangentU,
				ManifoldPoint.ContactMassTangentU,
				ManifoldPoint.NetPushOutTangentU,
				PushOutTangentU);					// Out

			CalculatePositionCorrectionTangent(
				Stiffness,
				ContactDeltaTangentV,
				ManifoldPoint.ContactMassTangentV,
				ManifoldPoint.NetPushOutTangentV,
				PushOutTangentV);					// Out

			// NOTE: This function modifies NetPushOutTangentU and V
			ApplyFrictionCone(
				StaticFriction,
				DynamicFriction,
				MaxFrictionPushOut,
				PushOutTangentU,									// InOut
				PushOutTangentV,									// InOut
				ManifoldPoint.NetPushOutTangentU,					// InOut
				ManifoldPoint.NetPushOutTangentV,					// InOut
				ManifoldPoint.StaticFrictionRatio);					// Out

			// Update the particle state based on the pushout
			const FVec3 PushOut = PushOutTangentU * ManifoldPoint.ContactTangentU + PushOutTangentV * ManifoldPoint.ContactTangentV;
			if (IsDynamic(0))
			{
				const FSolverVec3 DX0 = State.InvMs[0] * PushOut;
				const FSolverVec3 DR0 = ManifoldPoint.ContactTangentUAngular0 * PushOutTangentU + ManifoldPoint.ContactTangentVAngular0 * PushOutTangentV;
				FConstraintSolverBody& Body0 = SolverBody0();
				Body0.ApplyPositionDelta(DX0);
				Body0.ApplyRotationDelta(DR0);
			}
			if (IsDynamic(1))
			{
				const FSolverVec3 DX1 = -State.InvMs[1]* PushOut;
				const FSolverVec3 DR1 = ManifoldPoint.ContactTangentUAngular1 * -PushOutTangentU + ManifoldPoint.ContactTangentVAngular1 * -PushOutTangentV;
				FConstraintSolverBody& Body1 = SolverBody1();
				Body1.ApplyPositionDelta(DX1);
				Body1.ApplyRotationDelta(DR1);
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::ApplyPositionCorrectionNormal(
			const FSolverReal Stiffness,
			const FSolverReal ContactDeltaNormal,
			FPBDCollisionSolverManifoldPoint& ManifoldPoint)
		{
			FSolverReal PushOutNormal = FSolverReal(0);

			CalculatePositionCorrectionNormal(
				Stiffness,
				ContactDeltaNormal,
				ManifoldPoint.ContactMassNormal,
				ManifoldPoint.NetPushOutNormal,
				PushOutNormal);						// Out

			ManifoldPoint.NetPushOutNormal += PushOutNormal;

			// Update the particle state based on the pushout
			if (IsDynamic(0))
			{
				const FSolverVec3 DX0 = (State.InvMs[0] * PushOutNormal) * ManifoldPoint.ContactNormal;
				const FSolverVec3 DR0 = ManifoldPoint.ContactNormalAngular0 * PushOutNormal;
				FConstraintSolverBody& Body0 = SolverBody0();
				Body0.ApplyPositionDelta(DX0);
				Body0.ApplyRotationDelta(DR0);
			}
			if (IsDynamic(1))
			{
				const FSolverVec3 DX1 = (State.InvMs[1] * -PushOutNormal) * ManifoldPoint.ContactNormal;
				const FSolverVec3 DR1 = ManifoldPoint.ContactNormalAngular1 * -PushOutNormal;
				FConstraintSolverBody& Body1 = SolverBody1();
				Body1.ApplyPositionDelta(DX1);
				Body1.ApplyRotationDelta(DR1);
			}
		}


		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::ApplyVelocityCorrection(
			const FSolverReal Stiffness,
			const FSolverReal Dt,
			const FSolverReal DynamicFriction,
			const FSolverReal ContactVelocityDeltaNormal,
			const FSolverReal ContactVelocityDeltaTangent0,
			const FSolverReal ContactVelocityDeltaTangent1,
			const FSolverReal MinImpulseNormal,
			FPBDCollisionSolverManifoldPoint& ManifoldPoint)
		{
			FSolverReal ImpulseNormal = -Stiffness * ManifoldPoint.ContactMassNormal * ContactVelocityDeltaNormal;
			FSolverReal ImpulseTangentU = -Stiffness * ManifoldPoint.ContactMassTangentU * ContactVelocityDeltaTangent0;
			FSolverReal ImpulseTangentV = -Stiffness * ManifoldPoint.ContactMassTangentV * ContactVelocityDeltaTangent1;

			// Clamp the total impulse to be positive along the normal. We can apply a net negative impulse, 
			// but only to correct the velocity that was added by pushout (in which case MinImpulseNormal will be negative).
			if ((ManifoldPoint.NetImpulseNormal + ImpulseNormal) < MinImpulseNormal)
			{
				// We are trying to apply a net negative impulse larger than one to counteract the effective pushout impulse
				// so clamp the net impulse to be equal to minus the pushout impulse along the normal.
				ImpulseNormal = MinImpulseNormal - ManifoldPoint.NetImpulseNormal;
			}

			// Clamp the tangential impulses to the friction cone
			if ((DynamicFriction > 0) && (Dt > 0))
			{
				const FSolverReal MaxImpulseTangent = FMath::Max(FSolverReal(0), DynamicFriction * (ManifoldPoint.NetImpulseNormal + ImpulseNormal + ManifoldPoint.NetPushOutNormal / Dt));
				const FSolverReal MaxImpulseTangentSq = FMath::Square(MaxImpulseTangent);
				const FSolverReal ImpulseTangentSq = FMath::Square(ImpulseTangentU) + FMath::Square(ImpulseTangentV);
				if (ImpulseTangentSq > (MaxImpulseTangentSq + UE_SMALL_NUMBER))
				{
					const FSolverReal ImpulseTangentScale = MaxImpulseTangent * FMath::InvSqrt(ImpulseTangentSq);
					ImpulseTangentU *= ImpulseTangentScale;
					ImpulseTangentV *= ImpulseTangentScale;
				}
			}

			ManifoldPoint.NetImpulseNormal += ImpulseNormal;
			ManifoldPoint.NetImpulseTangentU += ImpulseTangentU;
			ManifoldPoint.NetImpulseTangentV += ImpulseTangentV;

			// Apply the velocity deltas from the impulse
			const FSolverVec3 Impulse = ImpulseNormal * ManifoldPoint.ContactNormal + ImpulseTangentU * ManifoldPoint.ContactTangentU + ImpulseTangentV * ManifoldPoint.ContactTangentV;
			if (IsDynamic(0))
			{
				const FSolverVec3 DV0 = State.InvMs[0] * Impulse;
				const FSolverVec3 DW0 = ManifoldPoint.ContactNormalAngular0 * ImpulseNormal + ManifoldPoint.ContactTangentUAngular0 * ImpulseTangentU + ManifoldPoint.ContactTangentVAngular0 * ImpulseTangentV;
				FConstraintSolverBody& Body0 = SolverBody0();
				Body0.ApplyVelocityDelta(DV0, DW0);
			}
			if (IsDynamic(1))
			{
				const FSolverVec3 DV1 = -State.InvMs[1]* Impulse;
				const FSolverVec3 DW1 = ManifoldPoint.ContactNormalAngular1 * -ImpulseNormal + ManifoldPoint.ContactTangentUAngular1 * -ImpulseTangentU + ManifoldPoint.ContactTangentVAngular1 * -ImpulseTangentV;
				FConstraintSolverBody& Body1 = SolverBody1();
				Body1.ApplyVelocityDelta(DV1, DW1);
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::ApplyVelocityCorrectionNormal(
			const FSolverReal Stiffness,
			const FSolverReal ContactVelocityDeltaNormal,
			const FSolverReal MinImpulseNormal,
			FPBDCollisionSolverManifoldPoint& ManifoldPoint)
		{
			FSolverReal ImpulseNormal = -(Stiffness * ManifoldPoint.ContactMassNormal) * ContactVelocityDeltaNormal;

			// See comments in ApplyVelocityCorrection
			if (ManifoldPoint.NetImpulseNormal + ImpulseNormal < MinImpulseNormal)
			{
				ImpulseNormal = MinImpulseNormal - ManifoldPoint.NetImpulseNormal;
			}

			ManifoldPoint.NetImpulseNormal += ImpulseNormal;

			// Calculate the velocity deltas from the impulse
			FSolverVec3 Impulse = ImpulseNormal * ManifoldPoint.ContactNormal;
			if (IsDynamic(0))
			{
				const FSolverVec3 DV0 = State.InvMs[0] * Impulse;
				const FSolverVec3 DW0 = ManifoldPoint.ContactNormalAngular0 * ImpulseNormal;
				FConstraintSolverBody& Body0 = SolverBody0();
				Body0.ApplyVelocityDelta(DV0, DW0);
			}
			if (IsDynamic(1))
			{
				const FSolverVec3 DV1 = -State.InvMs[1] * Impulse;
				const FSolverVec3 DW1 = ManifoldPoint.ContactNormalAngular1 * -ImpulseNormal;
				FConstraintSolverBody& Body1 = SolverBody1();
				Body1.ApplyVelocityDelta(DV1, DW1);
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::UpdateMass()
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			FSolverReal InvM0, InvM1;
			FSolverMatrix33 InvI0, InvI1;
			GetDynamicMassProperties(InvM0, InvI0, InvM1, InvI1);

			State.InvMs[0] = InvM0;
			State.InvMs[1] = InvM1;

			for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
			{
				FPBDCollisionSolverManifoldPoint& ManifoldPoint = State.ManifoldPoints[PointIndex];

				FSolverReal ContactMassInvNormal = FSolverReal(0);
				FSolverReal ContactMassInvTangentU = FSolverReal(0);
				FSolverReal ContactMassInvTangentV = FSolverReal(0);

				// These are not used if not initialized below so no need to clear
				//ContactNormalAngular0 = FSolverVec3(0);
				//ContactTangentUAngular0 = FSolverVec3(0);
				//ContactTangentVAngular0 = FSolverVec3(0);
				//ContactNormalAngular1 = FSolverVec3(0);
				//ContactTangentUAngular1 = FSolverVec3(0);
				//ContactTangentVAngular1 = FSolverVec3(0);

				if (IsDynamic(0))
				{
					const FSolverVec3 R0xN = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[0], ManifoldPoint.ContactNormal);
					const FSolverVec3 R0xU = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[0], ManifoldPoint.ContactTangentU);
					const FSolverVec3 R0xV = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[0], ManifoldPoint.ContactTangentV);

					ManifoldPoint.ContactNormalAngular0 = InvI0 * R0xN;
					ManifoldPoint.ContactTangentUAngular0 = InvI0 * R0xU;
					ManifoldPoint.ContactTangentVAngular0 = InvI0 * R0xV;

					ContactMassInvNormal += FSolverVec3::DotProduct(R0xN, ManifoldPoint.ContactNormalAngular0) + InvM0;
					ContactMassInvTangentU += FSolverVec3::DotProduct(R0xU, ManifoldPoint.ContactTangentUAngular0) + InvM0;
					ContactMassInvTangentV += FSolverVec3::DotProduct(R0xV, ManifoldPoint.ContactTangentVAngular0) + InvM0;
				}
				if (IsDynamic(1))
				{
					const FSolverVec3 R1xN = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[1], ManifoldPoint.ContactNormal);
					const FSolverVec3 R1xU = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[1], ManifoldPoint.ContactTangentU);
					const FSolverVec3 R1xV = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[1], ManifoldPoint.ContactTangentV);

					ManifoldPoint.ContactNormalAngular1 = InvI1 * R1xN;
					ManifoldPoint.ContactTangentUAngular1 = InvI1 * R1xU;
					ManifoldPoint.ContactTangentVAngular1 = InvI1 * R1xV;

					ContactMassInvNormal += FSolverVec3::DotProduct(R1xN, ManifoldPoint.ContactNormalAngular1) + InvM1;
					ContactMassInvTangentU += FSolverVec3::DotProduct(R1xU, ManifoldPoint.ContactTangentUAngular1) + InvM1;
					ContactMassInvTangentV += FSolverVec3::DotProduct(R1xV, ManifoldPoint.ContactTangentVAngular1) + InvM1;
				}

				ManifoldPoint.ContactMassNormal = (ContactMassInvNormal > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvNormal : FSolverReal(0);
				ManifoldPoint.ContactMassTangentU = (ContactMassInvTangentU > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentU : FSolverReal(0);
				ManifoldPoint.ContactMassTangentV = (ContactMassInvTangentV > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentV : FSolverReal(0);
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::UpdateMassNormal()
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			FSolverReal InvM0, InvM1;
			FSolverMatrix33 InvI0, InvI1;
			GetDynamicMassProperties(InvM0, InvI0, InvM1, InvI1);

			State.InvMs[0] = InvM0;
			State.InvMs[1] = InvM1;

			for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
			{
				FPBDCollisionSolverManifoldPoint& ManifoldPoint = State.ManifoldPoints[PointIndex];

				FSolverReal ContactMassInvNormal = FSolverReal(0);
				if (IsDynamic(0))
				{
					const FSolverVec3 R0xN = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[0], ManifoldPoint.ContactNormal);
					ManifoldPoint.ContactNormalAngular0 = InvI0 * R0xN;
					ContactMassInvNormal += FSolverVec3::DotProduct(R0xN, ManifoldPoint.ContactNormalAngular0) + InvM0;
				}
				if (IsDynamic(1))
				{
					const FSolverVec3 R1xN = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPoints[1], ManifoldPoint.ContactNormal);
					ManifoldPoint.ContactNormalAngular1 = InvI1 * R1xN;
					ContactMassInvNormal += FSolverVec3::DotProduct(R1xN, ManifoldPoint.ContactNormalAngular1) + InvM1;
				}
				ManifoldPoint.ContactMassNormal = (ContactMassInvNormal > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvNormal : FSolverReal(0);
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculateContactPositionErrorNormal(
			const FPBDCollisionSolverManifoldPoint& ManifoldPoint,
			const FSolverReal MaxPushOut,
			FSolverReal& OutContactDeltaNormal) const
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
			const FSolverVec3 ContactDelta0 = Body0.DP() + FSolverVec3::CrossProduct(Body0.DQ(), ManifoldPoint.RelativeContactPoints[0]);
			const FSolverVec3 ContactDelta1 = Body1.DP() + FSolverVec3::CrossProduct(Body1.DQ(), ManifoldPoint.RelativeContactPoints[1]);
			const FSolverVec3 ContactDelta = ContactDelta0 - ContactDelta1;
			OutContactDeltaNormal = ManifoldPoint.ContactDeltaNormal + FSolverVec3::DotProduct(ContactDelta, ManifoldPoint.ContactNormal);

			// NOTE: OutContactDeltaNormal is negative for penetration
			// NOTE: MaxPushOut == 0 disables the pushout limits
			if ((MaxPushOut > 0) && (OutContactDeltaNormal < -MaxPushOut))
			{
				OutContactDeltaNormal = -MaxPushOut;
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculateContactPositionErrorTangential(
			const FPBDCollisionSolverManifoldPoint& ManifoldPoint,
			FSolverReal& OutContactDeltaTangentU,
			FSolverReal& OutContactDeltaTangentV) const
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
			const FSolverVec3 ContactDelta0 = Body0.DP() + FSolverVec3::CrossProduct(Body0.DQ(), ManifoldPoint.RelativeContactPoints[0]);
			const FSolverVec3 ContactDelta1 = Body1.DP() + FSolverVec3::CrossProduct(Body1.DQ(), ManifoldPoint.RelativeContactPoints[1]);
			const FSolverVec3 ContactDelta = ContactDelta0 - ContactDelta1;
			OutContactDeltaTangentU = ManifoldPoint.ContactDeltaTangentU + FSolverVec3::DotProduct(ContactDelta, ManifoldPoint.ContactTangentU);
			OutContactDeltaTangentV = ManifoldPoint.ContactDeltaTangentV + FSolverVec3::DotProduct(ContactDelta, ManifoldPoint.ContactTangentV);
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculateContactVelocityError(
			const FPBDCollisionSolverManifoldPoint& ManifoldPoint,
			const FSolverReal DynamicFriction,
			const FSolverReal Dt,
			FSolverReal& OutContactVelocityDeltaNormal,
			FSolverReal& OutContactVelocityDeltaTangent0,
			FSolverReal& OutContactVelocityDeltaTangent1) const
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			const FSolverVec3 ContactVelocity0 = Body0.V() + FSolverVec3::CrossProduct(Body0.W(), ManifoldPoint.RelativeContactPoints[0]);
			const FSolverVec3 ContactVelocity1 = Body1.V() + FSolverVec3::CrossProduct(Body1.W(), ManifoldPoint.RelativeContactPoints[1]);
			const FSolverVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FSolverReal ContactVelocityNormal = FSolverVec3::DotProduct(ContactVelocity, ManifoldPoint.ContactNormal);
			const FSolverReal ContactVelocityTangent0 = FSolverVec3::DotProduct(ContactVelocity, ManifoldPoint.ContactTangentU);
			const FSolverReal ContactVelocityTangent1 = FSolverVec3::DotProduct(ContactVelocity, ManifoldPoint.ContactTangentV);

			OutContactVelocityDeltaNormal = (ContactVelocityNormal - ManifoldPoint.ContactTargetVelocityNormal);
			OutContactVelocityDeltaTangent0 = ContactVelocityTangent0;
			OutContactVelocityDeltaTangent1 = ContactVelocityTangent1;
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::CalculateContactVelocityErrorNormal(
			const FPBDCollisionSolverManifoldPoint& ManifoldPoint,
			FSolverReal& OutContactVelocityDeltaNormal) const
		{
			const FConstraintSolverBody& Body0 = SolverBody0();
			const FConstraintSolverBody& Body1 = SolverBody1();

			const FSolverVec3 ContactVelocity0 = Body0.V() + FSolverVec3::CrossProduct(Body0.W(), ManifoldPoint.RelativeContactPoints[0]);
			const FSolverVec3 ContactVelocity1 = Body1.V() + FSolverVec3::CrossProduct(Body1.W(), ManifoldPoint.RelativeContactPoints[1]);
			const FSolverVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FSolverReal ContactVelocityNormal = FSolverVec3::DotProduct(ContactVelocity, ManifoldPoint.ContactNormal);

			// Add up the errors in the velocity (current velocity - desired velocity)
			OutContactVelocityDeltaNormal = (ContactVelocityNormal - ManifoldPoint.ContactTargetVelocityNormal);
		}

		FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::ShouldSolveVelocity(
			const FPBDCollisionSolverManifoldPoint& ManifoldPoint) const
		{
			// We ensure positive separating velocity for close contacts even if they didn't receive a pushout
			return (ManifoldPoint.NetPushOutNormal > FSolverReal(0)) || (ManifoldPoint.ContactDeltaNormal < FSolverReal(0));
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::InitManifoldPoint(
			const int32 PointIndex,
			const FSolverReal Dt,
			const FSolverVec3& InRelativeContactPosition0,
			const FSolverVec3& InRelativeContactPosition1,
			const FSolverVec3& InWorldContactNormal,
			const FSolverVec3& InWorldContactTangentU,
			const FSolverVec3& InWorldContactTangentV,
			const FSolverReal InWorldContactDeltaNormal,
			const FSolverReal InWorldContactDeltaTangentU,
			const FSolverReal InWorldContactDeltaTangentV,
			const FSolverReal InWorldContactVelocityTargetNormal)
		{
			FPBDCollisionSolverManifoldPoint& ManifoldPoint = State.ManifoldPoints[PointIndex];

			ManifoldPoint.RelativeContactPoints[0] = InRelativeContactPosition0;
			ManifoldPoint.RelativeContactPoints[1] = InRelativeContactPosition1;
			ManifoldPoint.ContactNormal = InWorldContactNormal;
			ManifoldPoint.ContactTangentU = InWorldContactTangentU;
			ManifoldPoint.ContactTangentV = InWorldContactTangentV;
			ManifoldPoint.ContactDeltaNormal = InWorldContactDeltaNormal;
			ManifoldPoint.ContactDeltaTangentU = InWorldContactDeltaTangentU;
			ManifoldPoint.ContactDeltaTangentV = InWorldContactDeltaTangentV;
			ManifoldPoint.ContactTargetVelocityNormal = InWorldContactVelocityTargetNormal;

			ManifoldPoint.NetPushOutNormal = FSolverReal(0);
			ManifoldPoint.NetPushOutTangentU = FSolverReal(0);
			ManifoldPoint.NetPushOutTangentV = FSolverReal(0);
			ManifoldPoint.NetImpulseNormal = FSolverReal(0);
			ManifoldPoint.NetImpulseTangentU = FSolverReal(0);
			ManifoldPoint.NetImpulseTangentV = FSolverReal(0);
			ManifoldPoint.StaticFrictionRatio = FSolverReal(0);
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::FinalizeManifold()
		{
			UpdateMass();
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::SolvePositionWithFriction(
			const FSolverReal Dt,
			const FSolverReal MaxPushOut)
		{
			// Accumulate net pushout for friction limits below
			bool bApplyFriction = false;
			bool bApplyPointFriction[MaxPointsPerConstraint] = { false, };
			FSolverReal NumFrictionContacts = FSolverReal(0);	// NOTE: deliberately not an int
			FSolverReal TotalPushOutNormal = FSolverReal(0);

			// Apply the position correction along the normal and determine if we want to run friction on each point
			for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
			{
				FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

				FSolverReal ContactDeltaNormal;
				CalculateContactPositionErrorNormal(SolverManifoldPoint, MaxPushOut, ContactDeltaNormal);

				// Apply a normal correction if we still have penetration or if we are now separated but have previously applied a correction that we may want to undo
				const bool bProcessManifoldPoint = (ContactDeltaNormal < FSolverReal(0)) || (SolverManifoldPoint.NetPushOutNormal > FSolverReal(UE_SMALL_NUMBER));
				if (bProcessManifoldPoint)
				{
					ApplyPositionCorrectionNormal(
						State.Stiffness,
						ContactDeltaNormal,
						SolverManifoldPoint);
				}

				// Friction gets updated for any point with a net normal correction or where we have previously had a normal correction and 
				// already applied friction (in which case we may need to zero it)
				if (SolverManifoldPoint.NetPushOutNormal > 0)
				{
					TotalPushOutNormal += SolverManifoldPoint.NetPushOutNormal;
					NumFrictionContacts += FSolverReal(1);
				}
				if ((SolverManifoldPoint.NetPushOutNormal > 0) || (SolverManifoldPoint.NetPushOutTangentU != 0) || (SolverManifoldPoint.NetPushOutTangentV != 0))
				{
					bApplyFriction = true;
					bApplyPointFriction[PointIndex] = ((SolverManifoldPoint.NetPushOutNormal > 0) || (SolverManifoldPoint.NetPushOutTangentU != 0) || (SolverManifoldPoint.NetPushOutTangentV != 0));
				}
			}

			// Apply the tangential position correction if required
			if (bApplyFriction)
			{
				// We clip the tangential correction at each contact to the friction cone, but we use to average impulse
				// among all contacts as the clipping limit. This is not really correct but it is much more stable to 
				// differences in contacts from tick to tick
				// @todo(chaos): try a decaying maximum per contact point rather than an average (again - we had that once!)
				const FSolverReal FrictionMaxPushOut = (NumFrictionContacts > FSolverReal(0)) ? TotalPushOutNormal / NumFrictionContacts : FSolverReal(0);
				const FSolverReal FrictionStiffness = State.Stiffness * CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;

				for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
				{
					FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
					if (bApplyPointFriction[PointIndex])
					{
						FSolverReal ContactDeltaTangentU, ContactDeltaTangentV;
						CalculateContactPositionErrorTangential(SolverManifoldPoint, ContactDeltaTangentU, ContactDeltaTangentV);

						ApplyPositionCorrectionTangential(
							FrictionStiffness,
							State.StaticFriction,
							State.DynamicFriction,
							FrictionMaxPushOut,
							ContactDeltaTangentU,
							ContactDeltaTangentV,
							SolverManifoldPoint);
					}
				}
			}
		}

		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::SolvePositionNoFriction(
			const FSolverReal Dt,
			const FSolverReal MaxPushOut)
		{
			// Apply the position correction so that all contacts have zero separation
			for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
			{
				FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

				FSolverReal ContactDeltaNormal;
				CalculateContactPositionErrorNormal(SolverManifoldPoint, MaxPushOut, ContactDeltaNormal);

				const bool bProcessManifoldPoint = (ContactDeltaNormal < FSolverReal(0)) || (SolverManifoldPoint.NetPushOutNormal > FSolverReal(UE_SMALL_NUMBER));
				if (bProcessManifoldPoint)
				{
					ApplyPositionCorrectionNormal(
						State.Stiffness,
						ContactDeltaNormal,
						SolverManifoldPoint);
				}
			}
		}

		/**
		 * @brief Calculate and apply the velocity correction for this iteration
		 * @return true if we need to run more iterations, false if we did not apply any correction
		*/
		FORCEINLINE_DEBUGGABLE void FPBDCollisionSolver::SolveVelocity(
			const FSolverReal Dt,
			const bool bApplyDynamicFriction)
		{
			// Apply restitution at the average contact point
			// This means we don't need to run as many iterations to get stable bouncing
			// It also helps with zero restitution to counter any velocioty added by the PBD solve
			const bool bSolveAverageContact = (NumManifoldPoints() > 1) && CVars::bChaos_PBDCollisionSolver_Velocity_AveragePointEnabled;
			if (bSolveAverageContact)
			{
				SolveVelocityAverage(Dt);
			}

			// NOTE: this dynamic friction implementation is iteration-count sensitive
			// @todo(chaos): fix iteration count dependence of dynamic friction
			const FSolverReal DynamicFriction = (bApplyDynamicFriction && (Dt > 0) && CVars::bChaos_PBDCollisionSolver_Velocity_FrictionEnabled) ? State.VelocityFriction : FSolverReal(0);

			for (int32 PointIndex = 0; PointIndex < NumManifoldPoints(); ++PointIndex)
			{
				FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

				if (ShouldSolveVelocity(SolverManifoldPoint))
				{
					const FSolverReal MinImpulseNormal = FMath::Min(FSolverReal(0), -SolverManifoldPoint.NetPushOutNormal / Dt);

					if (DynamicFriction > 0)
					{
						FSolverReal ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV;
						CalculateContactVelocityError(SolverManifoldPoint, DynamicFriction, Dt, ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV);

						ApplyVelocityCorrection(
							State.Stiffness,
							Dt,
							DynamicFriction,
							ContactVelocityDeltaNormal,
							ContactVelocityDeltaTangentU,
							ContactVelocityDeltaTangentV,
							MinImpulseNormal,
							SolverManifoldPoint);
					}
					else
					{
						FSolverReal ContactVelocityDeltaNormal;
						CalculateContactVelocityErrorNormal(SolverManifoldPoint, ContactVelocityDeltaNormal);

						ApplyVelocityCorrectionNormal(
							State.Stiffness,
							ContactVelocityDeltaNormal,
							MinImpulseNormal,
							SolverManifoldPoint);
					}
				}
			}
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * A helper for solving arrays of constraints
		 */
		class FPBDCollisionSolverHelper
		{
		public:
			static void SolvePositionNoFriction(const TArrayView<FPBDCollisionSolver>& CollisionSolvers, const FSolverReal Dt, const FSolverReal MaxPushOut);
			static void SolvePositionWithFriction(const TArrayView<FPBDCollisionSolver>& CollisionSolvers, const FSolverReal Dt, const FSolverReal MaxPushOut);
			static void SolveVelocity(const TArrayView<FPBDCollisionSolver>& CollisionSolvers, const FSolverReal Dt, const bool bApplyDynamicFriction);

			static void CheckISPC();
		};

	}	// namespace Private
}	// namespace Chaos

