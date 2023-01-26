// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBody.h"
#include "Chaos/Simd4.h"

// Set to 0 to use a linearized error calculation, and set to 1 to use a non-linear error calculation in collision detection. 
// In principle nonlinear is more accurate when large rotation corrections occur, but this is not too important for collisions because 
// when the bodies settle the corrections are small. The linearized version is significantly faster than the non-linear version because 
// the non-linear version requires a quaternion multiply and renormalization whereas the linear version is just a cross product.
#define CHAOS_NONLINEAR_COLLISIONS_ENABLED 0

namespace Chaos
{
	class FManifoldPoint;
	class FPBDCollisionConstraint;

	namespace Private
	{
		class FPBDCollisionSolverSimd;
	}

	namespace Private
	{
		template<int TNumLanes>
		using TSolverBodyPtrSimd = TSimdValue<FSolverBody*, TNumLanes>;

		template<int TNumLanes>
		struct TSolverBodyPtrPairSimd
		{
			TSolverBodyPtrSimd<TNumLanes> Body0;
			TSolverBodyPtrSimd<TNumLanes> Body1;
		};

		/**
		 * @brief A SIMD row of contact points from a set of FPBDCollisionSolverSimd
		*/
		template<int TNumLanes>
		class TPBDCollisionSolverManifoldPointsSimd
		{
		public:
			using FSimdVec3f = TSimdVec3f<TNumLanes>;
			using FSimdRealf = TSimdRealf<TNumLanes>;
			using FSimdInt32 = TSimdInt32<TNumLanes>;
			using FSimdSelector = TSimdSelector<TNumLanes>;
			using FSimdSolverBodyPtr = TSolverBodyPtrSimd<TNumLanes>;

			TPBDCollisionSolverManifoldPointsSimd()
			{
				SimdConstraintIndex.SetValues(INDEX_NONE);
				SimdManifoldPointIndex.SetValues(INDEX_NONE);
			}

			void SetSharedData(
				const int32 LaneIndex,
				const int32 InConstraintIndex,
				const int32 InManifoldPointIndex)
			{
				SimdConstraintIndex.SetValue(LaneIndex, InConstraintIndex);
				SimdManifoldPointIndex.SetValue(LaneIndex, InManifoldPointIndex);
			}

			/**
			 * @brief Initialize contact geometry (contact positions, normal, etc)
			*/
			void InitContact(
				const int32 LaneIndex,
				const FSolverVec3& InRelativeContactPosition0,
				const FSolverVec3& InRelativeContactPosition1,
				const FSolverVec3& InWorldContactNormal,
				const FSolverVec3& InWorldContactTangentU,
				const FSolverVec3& InWorldContactTangentV,
				const FSolverReal InWorldContactDeltaNormal,
				const FSolverReal InWorldContactDeltaTangentU,
				const FSolverReal InWorldContactDeltaTangentV,
				const FSolverReal InWorldContactVelocityTargetNormal,
				const FSolverReal InStiffness,
				const FSolverReal InStaticFriction,
				const FSolverReal InDynamicFriction,
				const FSolverReal InVelocityFriction)
			{
				SimdRelativeContactPoint0.SetValue(LaneIndex, InRelativeContactPosition0);
				SimdRelativeContactPoint1.SetValue(LaneIndex, InRelativeContactPosition1);
				SimdContactNormal.SetValue(LaneIndex, InWorldContactNormal);
				SimdContactTangentU.SetValue(LaneIndex, InWorldContactTangentU);
				SimdContactTangentV.SetValue(LaneIndex, InWorldContactTangentV);
				SimdContactDeltaNormal.SetValue(LaneIndex, InWorldContactDeltaNormal);
				SimdContactDeltaTangentU.SetValue(LaneIndex, InWorldContactDeltaTangentU);
				SimdContactDeltaTangentV.SetValue(LaneIndex, InWorldContactDeltaTangentV);
				SimdContactTargetVelocityNormal.SetValue(LaneIndex, InWorldContactVelocityTargetNormal);

				SimdStiffness.SetValue(LaneIndex, InStiffness);
				SimdStaticFriction.SetValue(LaneIndex, InStaticFriction);
				SimdDynamicFriction.SetValue(LaneIndex, InDynamicFriction);
				SimdVelocityFriction.SetValue(LaneIndex, InVelocityFriction);
			}

			/**
			 * @brief Initialize contact state (net impulses, masses, etc). Must come after InitContact
			 * because the mass properties depend on contact offsets etc.
			*/
			void FinalizeContact(
				const int32 LaneIndex,
				const FConstraintSolverBody& Body0,
				const FConstraintSolverBody& Body1)
			{
				SimdNetPushOutNormal.SetValue(LaneIndex, 0);
				SimdNetPushOutTangentU.SetValue(LaneIndex, 0);
				SimdNetPushOutTangentV.SetValue(LaneIndex, 0);
				SimdNetImpulseNormal.SetValue(LaneIndex, 0);
				SimdNetImpulseTangentU.SetValue(LaneIndex, 0);
				SimdNetImpulseTangentV.SetValue(LaneIndex, 0);
				SimdStaticFrictionRatio.SetValue(LaneIndex, 0);

				UpdateMass(LaneIndex, Body0, Body1);
			}

			/**
			 * @brief Update the cached mass properties based on the current body transforms
			*/
			void UpdateMass(
				const int32 LaneIndex,
				const FConstraintSolverBody& Body0,
				const FConstraintSolverBody& Body1)
			{
				FSolverReal ContactMassInvNormal = FSolverReal(0);
				FSolverReal ContactMassInvTangentU = FSolverReal(0);
				FSolverReal ContactMassInvTangentV = FSolverReal(0);

				SimdInvM0.SetValue(LaneIndex, 0);
				SimdInvM1.SetValue(LaneIndex, 0);
				SimdContactNormalAngular0.SetValue(LaneIndex, FVec3f(0));
				SimdContactTangentUAngular0.SetValue(LaneIndex, FVec3f(0));
				SimdContactTangentVAngular0.SetValue(LaneIndex, FVec3f(0));
				SimdContactNormalAngular1.SetValue(LaneIndex, FVec3f(0));
				SimdContactTangentUAngular1.SetValue(LaneIndex, FVec3f(0));
				SimdContactTangentVAngular1.SetValue(LaneIndex, FVec3f(0));

				const FVec3f ContactNormal = SimdContactNormal.GetValue(LaneIndex);
				const FVec3f ContactTangentU = SimdContactTangentU.GetValue(LaneIndex);
				const FVec3f ContactTangentV = SimdContactTangentV.GetValue(LaneIndex);

				if (Body0.IsDynamic())
				{
					const FVec3f RelativeContactPoint0 = SimdRelativeContactPoint0.GetValue(LaneIndex);

					const FSolverVec3 R0xN = FSolverVec3::CrossProduct(RelativeContactPoint0, ContactNormal);
					const FSolverVec3 R0xU = FSolverVec3::CrossProduct(RelativeContactPoint0, ContactTangentU);
					const FSolverVec3 R0xV = FSolverVec3::CrossProduct(RelativeContactPoint0, ContactTangentV);

					const FSolverMatrix33 InvI0 = Body0.InvI();
					const FSolverVec3 IR0xN = InvI0 * R0xN;
					const FSolverVec3 IR0xU = InvI0 * R0xU;
					const FSolverVec3 IR0xV = InvI0 * R0xV;

					SimdInvM0.SetValue(LaneIndex, Body0.InvM());

					SimdContactNormalAngular0.SetValue(LaneIndex, IR0xN);
					SimdContactTangentUAngular0.SetValue(LaneIndex, IR0xU);
					SimdContactTangentVAngular0.SetValue(LaneIndex, IR0xV);

					ContactMassInvNormal += FSolverVec3::DotProduct(R0xN, IR0xN) + Body0.InvM();
					ContactMassInvTangentU += FSolverVec3::DotProduct(R0xU, IR0xU) + Body0.InvM();
					ContactMassInvTangentV += FSolverVec3::DotProduct(R0xV, IR0xV) + Body0.InvM();
				}
				if (Body1.IsDynamic())
				{
					const FVec3f RelativeContactPoint1 = SimdRelativeContactPoint1.GetValue(LaneIndex);

					const FSolverVec3 R1xN = FSolverVec3::CrossProduct(RelativeContactPoint1, ContactNormal);
					const FSolverVec3 R1xU = FSolverVec3::CrossProduct(RelativeContactPoint1, ContactTangentU);
					const FSolverVec3 R1xV = FSolverVec3::CrossProduct(RelativeContactPoint1, ContactTangentV);

					const FSolverMatrix33 InvI1 = Body1.InvI();
					const FSolverVec3 IR1xN = InvI1 * R1xN;
					const FSolverVec3 IR1xU = InvI1 * R1xU;
					const FSolverVec3 IR1xV = InvI1 * R1xV;

					SimdInvM1.SetValue(LaneIndex, Body1.InvM());

					SimdContactNormalAngular1.SetValue(LaneIndex, IR1xN);
					SimdContactTangentUAngular1.SetValue(LaneIndex, IR1xU);
					SimdContactTangentVAngular1.SetValue(LaneIndex, IR1xV);

					ContactMassInvNormal += FSolverVec3::DotProduct(R1xN, IR1xN) + Body1.InvM();
					ContactMassInvTangentU += FSolverVec3::DotProduct(R1xU, IR1xU) + Body1.InvM();
					ContactMassInvTangentV += FSolverVec3::DotProduct(R1xV, IR1xV) + Body1.InvM();
				}

				SimdContactMassNormal.SetValue(LaneIndex, (ContactMassInvNormal > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvNormal : FSolverReal(0));
				SimdContactMassTangentU.SetValue(LaneIndex, (ContactMassInvTangentU > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentU : FSolverReal(0));
				SimdContactMassTangentV.SetValue(LaneIndex, (ContactMassInvTangentV > FSolverReal(UE_SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentV : FSolverReal(0));
			}

			/**
			 * @brief Update the contact mass for the normal correction
			 * This is used by shock propagation.
			*/
			void UpdateMassNormal(
				const int32 LaneIndex,
				FConstraintSolverBody& Body0,
				FConstraintSolverBody& Body1)
			{
			}

			FORCEINLINE void GatherBodyCorrections(
				const FSimdSelector& IsValid,
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1,
				FSimdVec3f& DP0,
				FSimdVec3f& DQ0,
				FSimdVec3f& DP1,
				FSimdVec3f& DQ1)
			{
				for (int32 LaneIndex = 0; LaneIndex < TNumLanes; ++LaneIndex)
				{
					if (IsValid.GetValue(LaneIndex))
					{
						const FSolverBody* LaneBody0 = Body0.GetValue(LaneIndex);
						const FSolverBody* LaneBody1 = Body1.GetValue(LaneIndex);
						DP0.SetValue(LaneIndex, LaneBody0->DP());
						DQ0.SetValue(LaneIndex, LaneBody0->DQ());
						DP1.SetValue(LaneIndex, LaneBody1->DP());
						DQ1.SetValue(LaneIndex, LaneBody1->DQ());
					}
				}
			}

			FORCEINLINE void ScatterBodyCorrections(
				const FSimdSelector& IsValid,
				const FSimdVec3f& DP0,
				const FSimdVec3f& DQ0,
				const FSimdVec3f& DP1,
				const FSimdVec3f& DQ1,
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1)
			{
				for (int32 LaneIndex = 0; LaneIndex < TNumLanes; ++LaneIndex)
				{
					if (IsValid.GetValue(LaneIndex))
					{
						FSolverBody* LaneBody0 = Body0.GetValue(LaneIndex);
						FSolverBody* LaneBody1 = Body1.GetValue(LaneIndex);
						LaneBody0->SetDP(DP0.GetValue(LaneIndex));
						LaneBody0->SetDQ(DQ0.GetValue(LaneIndex));
						LaneBody1->SetDP(DP1.GetValue(LaneIndex));
						LaneBody1->SetDQ(DQ1.GetValue(LaneIndex));
					}
				}
			}

			void SolvePositionNoFriction(
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1,
				const FSimd4Realf& MaxPushOut)
			{
				// Which lanes a have a valid constraint setup?
				const FSimdSelector IsValid = SimdGreaterEqual(SimdConstraintIndex, FSimdInt32::Zero());

				// Get the current corrections for each body. 
				// NOTE: This is a gather operation
				FSimdVec3f DP0, DQ0, DP1, DQ1;
				GatherBodyCorrections(IsValid, Body0, Body1, DP0, DQ0, DP1, DQ1);

				// Calculate the contact error
				const FSimdVec3f DQ0xR0 = SimdCrossProduct(DQ0, SimdRelativeContactPoint0);
				const FSimdVec3f DQ1xR1 = SimdCrossProduct(DQ1, SimdRelativeContactPoint1);
				const FSimdVec3f ContactDelta0 = SimdAdd(DP0, DQ0xR0);
				const FSimdVec3f ContactDelta1 = SimdAdd(DP1, DQ1xR1);
				const FSimdVec3f ContactDelta = SimdSubtract(ContactDelta0, ContactDelta1);
				FSimdRealf ContactErrorNormal = SimdAdd(SimdContactDeltaNormal, SimdDotProduct(ContactDelta, SimdContactNormal));

				// Apply MaxPushOut clamping if required
				//if ((MaxPushOut > 0) && (ContactErrorNormal < -MaxPushOut)) { ContactErrorNormal = -MaxPushOut; }
				const FSimdRealf NegMaxPushOut = SimdNegate(MaxPushOut);
				const FSimdSelector ShouldClampError = SimdAnd(SimdLess(NegMaxPushOut, FSimdRealf::Zero()), SimdLess(ContactErrorNormal, NegMaxPushOut));
				ContactErrorNormal = SimdSelect(ShouldClampError, NegMaxPushOut, ContactErrorNormal);

				// Determine which lanes to process: only those with an overlap or that applied a pushout on a prior iteration
				const FSimdSelector IsErrorNegative = SimdLess(ContactErrorNormal, FSimdRealf::Zero());
				const FSimdSelector IsNetPushOutPositive = SimdGreater(SimdNetPushOutNormal, FSimdRealf::Zero());
				const FSimdSelector ShouldProcess = SimdAnd(IsValid, SimdOr(IsErrorNegative, IsNetPushOutPositive));

				// If all lanes are to be skipped, early-out
				if (!SimdAnyTrue(ShouldProcess))
				{
					return;
				}

				// Zero out the error for points we should not process so we don't apply a correction for them
				ContactErrorNormal = SimdSelect(ShouldProcess, ContactErrorNormal, FSimdRealf::Zero());

				FSimdRealf PushOutNormal = SimdNegate(SimdMultiply(ContactErrorNormal, SimdMultiply(SimdStiffness, SimdContactMassNormal)));

				// Unilateral constraint: Net-negative pushout is not allowed, but
				// PushOutNormal may be negative on any iteration as long as the net is positive
				// If the net goes negative, apply a pushout to make it zero
				const FSimdSelector IsPositive = SimdGreater(SimdAdd(SimdNetPushOutNormal, PushOutNormal), FSimdRealf::Zero());
				PushOutNormal = SimdSelect(IsPositive, PushOutNormal, SimdNegate(SimdNetPushOutNormal));

				// New net pushout
				SimdNetPushOutNormal = SimdAdd(PushOutNormal, SimdNetPushOutNormal);

				// Convert the positional impulse into position and rotation corrections for each body
				const FSimdVec3f DDP0 = SimdMultiply(SimdContactNormal, SimdMultiply(SimdInvM0, PushOutNormal));
				const FSimdVec3f DDQ0 = SimdMultiply(SimdContactNormalAngular0, PushOutNormal);
				const FSimdVec3f DDP1 = SimdMultiply(SimdContactNormal, SimdMultiply(SimdInvM1, PushOutNormal));
				const FSimdVec3f DDQ1 = SimdMultiply(SimdContactNormalAngular1, PushOutNormal);

				DP0 = SimdAdd(DP0, DDP0);
				DQ0 = SimdAdd(DQ0, DDQ0);
				DP1 = SimdSubtract(DP1, DDP1);
				DQ1 = SimdSubtract(DQ1, DDQ1);

				// Update the corrections on the bodies. 
				// NOTE: This is a scatter operation
				ScatterBodyCorrections(IsValid, DP0, DQ0, DP1, DQ1, Body0, Body1);
			}

			void SolvePositionWithFriction(
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1,
				const FSimd4Realf& MaxPushOut,
				const FSimd4Realf& FrictionStiffnessScale)
			{
				// Which lanes a have a valid constraint setup?
				const FSimdSelector IsValid = SimdGreaterEqual(SimdConstraintIndex, FSimdInt32::Zero());

				// Get the current corrections for each body. 
				// NOTE: This is a gather operation
				FSimdVec3f DP0, DQ0, DP1, DQ1;
				GatherBodyCorrections(IsValid, Body0, Body1, DP0, DQ0, DP1, DQ1);

				// Calculate the contact error
				const FSimdVec3f DQ0xR0 = SimdCrossProduct(DQ0, SimdRelativeContactPoint0);
				const FSimdVec3f DQ1xR1 = SimdCrossProduct(DQ1, SimdRelativeContactPoint1);
				const FSimdVec3f ContactDelta0 = SimdAdd(DP0, DQ0xR0);
				const FSimdVec3f ContactDelta1 = SimdAdd(DP1, DQ1xR1);
				const FSimdVec3f ContactDelta = SimdSubtract(ContactDelta0, ContactDelta1);
				FSimdRealf ContactErrorNormal = SimdAdd(SimdContactDeltaNormal, SimdDotProduct(ContactDelta, SimdContactNormal));

				// Apply MaxPushOut clamping if required
				//if ((MaxPushOut > 0) && (ContactErrorNormal < -MaxPushOut)) { ContactErrorNormal = -MaxPushOut; }
				const FSimdRealf NegMaxPushOut = SimdNegate(MaxPushOut);
				const FSimdSelector ShouldClampError = SimdAnd(SimdLess(NegMaxPushOut, FSimdRealf::Zero()), SimdLess(ContactErrorNormal, NegMaxPushOut));
				ContactErrorNormal = SimdSelect(ShouldClampError, NegMaxPushOut, ContactErrorNormal);

				// Determine which lanes to process: only those with an overlap or that applied a pushout on a prior iteration
				const FSimdSelector IsErrorNegative = SimdLess(ContactErrorNormal, FSimdRealf::Zero());
				const FSimdSelector IsNetPushOutPositive = SimdGreater(SimdNetPushOutNormal, FSimdRealf::Zero());
				const FSimdSelector ShouldProcess = SimdAnd(IsValid, SimdOr(IsErrorNegative, IsNetPushOutPositive));

				// If all lanes are to be skipped, early-out
				//if (SimdAnyTrue(ShouldProcess))
				{
					// Zero out the error for points we should not process so we don't apply a correction for them
					ContactErrorNormal = SimdSelect(ShouldProcess, ContactErrorNormal, FSimdRealf::Zero());

					FSimdRealf PushOutNormal = SimdNegate(SimdMultiply(SimdStiffness, SimdMultiply(ContactErrorNormal, SimdContactMassNormal)));

					// Unilateral constraint: Net-negative pushout is not allowed, but
					// PushOutNormal may be negative on any iteration as long as the net is positive
					// If the net goes negative, apply a pushout to make it zero
					const FSimdSelector IsPositive = SimdGreater(SimdAdd(SimdNetPushOutNormal, PushOutNormal), FSimdRealf::Zero());
					PushOutNormal = SimdSelect(IsPositive, PushOutNormal, SimdNegate(SimdNetPushOutNormal));

					// New net normal pushout
					SimdNetPushOutNormal = SimdAdd(PushOutNormal, SimdNetPushOutNormal);
					SimdStaticFrictionRatio = FSimdRealf::One();

					// Convert the positional impulse into position and rotation corrections for each body
					FSimdVec3f DDP0 = SimdMultiply(SimdContactNormal, SimdMultiply(SimdInvM0, PushOutNormal));
					FSimdVec3f DDQ0 = SimdMultiply(SimdContactNormalAngular0, PushOutNormal);
					FSimdVec3f DDP1 = SimdMultiply(SimdContactNormal, SimdMultiply(SimdInvM1, PushOutNormal));
					FSimdVec3f DDQ1 = SimdMultiply(SimdContactNormalAngular1, PushOutNormal);

					// Should we apply tangential corrections for friction? 
					// bUpdateFriction = ((NetPushOutNormal > 0) || (NetPushOutTangentU != 0) || (NetPushOutTangentV != 0))
					const FSimdSelector HasNormalPushout = SimdGreater(SimdNetPushOutNormal, FSimdRealf::Zero());
					const FSimdSelector HasTangentUPushout = SimdNotEqual(SimdNetPushOutTangentU, FSimdRealf::Zero());
					const FSimdSelector HasTangentVPushout = SimdNotEqual(SimdNetPushOutTangentV, FSimdRealf::Zero());
					//const FSimdSelector UpdateFriction = SimdAnd(IsValid, SimdOr(HasNormalPushout, SimdOr(HasTangentUPushout, HasTangentVPushout)));
					//if (SimdAnyTrue(UpdateFriction))
					{
						// Calculate tangential errors
						const FSimdRealf ContactErrorTangentU = SimdAdd(SimdContactDeltaTangentU, SimdDotProduct(ContactDelta, SimdContactTangentU));
						const FSimdRealf ContactErrorTangentV = SimdAdd(SimdContactDeltaTangentV, SimdDotProduct(ContactDelta, SimdContactTangentV));

						// Calculate tangential correction
						const FSimdRealf FrictionStiffness = SimdMultiply(FrictionStiffnessScale, SimdStiffness);
						FSimdRealf PushOutTangentU = SimdNegate(SimdMultiply(FrictionStiffness, SimdMultiply(ContactErrorTangentU, SimdContactMassTangentU)));
						FSimdRealf PushOutTangentV = SimdNegate(SimdMultiply(FrictionStiffness, SimdMultiply(ContactErrorTangentV, SimdContactMassTangentV)));

						// New net tangential pushouts
						FSimdRealf NetPushOutTangentU = SimdAdd(SimdNetPushOutTangentU, PushOutTangentU);
						FSimdRealf NetPushOutTangentV = SimdAdd(SimdNetPushOutTangentV, PushOutTangentV);
						FSimdRealf StaticFrictionRatio = SimdStaticFrictionRatio;

						// If a lane has a zero normal pushout, we need to undo all previously applied friction
						//if (!SimdAllTrue(HasNormalPushout))
						{
							PushOutTangentU = SimdSelect(HasNormalPushout, PushOutTangentU, SimdNegate(SimdNetPushOutTangentU));
							PushOutTangentV = SimdSelect(HasNormalPushout, PushOutTangentV, SimdNegate(SimdNetPushOutTangentV));
							NetPushOutTangentU = SimdSelect(HasNormalPushout, NetPushOutTangentU, FSimdRealf::Zero());
							NetPushOutTangentV = SimdSelect(HasNormalPushout, NetPushOutTangentV, FSimdRealf::Zero());
							StaticFrictionRatio = SimdSelect(HasNormalPushout, StaticFrictionRatio, FSimdRealf::Zero());
						}

						// Apply cone limits to tangential pushouts on lanes that exceed the limit
						// NOTE: if HasNormalPushout is false in any lane, we have already zeroed the net pushout and don't need to do anything here
						const FSimdSelector ApplyFrictionCone = SimdAnd(IsValid, SimdAnd(HasNormalPushout, SimdOr(HasTangentUPushout, HasTangentVPushout)));
						//if (SimdAnyTrue(ApplyFrictionCone))
						{
							const FSimdRealf MaxPushOutTangent = SimdNetPushOutNormal;
							const FSimdRealf MaxStaticPushOutTangentSq = SimdSquare(SimdMultiply(SimdStaticFriction, MaxPushOutTangent));
							const FSimdRealf NetPushOutTangentSq = SimdAdd(SimdSquare(NetPushOutTangentU), SimdSquare(NetPushOutTangentV));
							const FSimdSelector ExceededFrictionCone = SimdAnd(ApplyFrictionCone, SimdGreater(NetPushOutTangentSq, MaxStaticPushOutTangentSq));
							//if (SimdAnyTrue(ExceededFrictionCone))
							{
								const FSimdRealf MaxDynamicPushOutTangent = SimdMultiply(SimdDynamicFriction, MaxPushOutTangent);
								const FSimdRealf FrictionMultiplier = SimdMultiply(MaxDynamicPushOutTangent, SimdInvSqrt(NetPushOutTangentSq));
								const FSimdRealf ClampedNetPushOutTangentU = SimdMultiply(FrictionMultiplier, NetPushOutTangentU);
								const FSimdRealf ClampedNetPushOutTangentV = SimdMultiply(FrictionMultiplier, NetPushOutTangentV);
								const FSimdRealf ClampedPushOutTangentU = SimdSubtract(ClampedNetPushOutTangentU, SimdNetPushOutTangentU);
								const FSimdRealf ClampedPushOutTangentV = SimdSubtract(ClampedNetPushOutTangentV, SimdNetPushOutTangentV);

								PushOutTangentU = SimdSelect(ExceededFrictionCone, ClampedPushOutTangentU, PushOutTangentU);
								PushOutTangentV = SimdSelect(ExceededFrictionCone, ClampedPushOutTangentV, PushOutTangentV);
								NetPushOutTangentU = SimdSelect(ExceededFrictionCone, ClampedNetPushOutTangentU, NetPushOutTangentU);
								NetPushOutTangentV = SimdSelect(ExceededFrictionCone, ClampedNetPushOutTangentV, NetPushOutTangentV);
								StaticFrictionRatio = SimdSelect(ExceededFrictionCone, FrictionMultiplier, StaticFrictionRatio);
							}
						}

						SimdNetPushOutTangentU = NetPushOutTangentU;
						SimdNetPushOutTangentV = NetPushOutTangentV;
						SimdStaticFrictionRatio = StaticFrictionRatio;

						// Add the tangential corrections to the applied correction
						DDP0 = SimdAdd(DDP0, SimdMultiply(SimdContactTangentU, SimdMultiply(SimdInvM0, PushOutTangentU)));
						DDP0 = SimdAdd(DDP0, SimdMultiply(SimdContactTangentV, SimdMultiply(SimdInvM0, PushOutTangentV)));
						DDQ0 = SimdAdd(DDQ0, SimdMultiply(SimdContactTangentUAngular0, PushOutTangentU));
						DDQ0 = SimdAdd(DDQ0, SimdMultiply(SimdContactTangentVAngular0, PushOutTangentV));
						DDP1 = SimdAdd(DDP1, SimdMultiply(SimdContactTangentU, SimdMultiply(SimdInvM1, PushOutTangentU)));
						DDP1 = SimdAdd(DDP1, SimdMultiply(SimdContactTangentV, SimdMultiply(SimdInvM1, PushOutTangentV)));
						DDQ1 = SimdAdd(DDQ1, SimdMultiply(SimdContactTangentUAngular1, PushOutTangentU));
						DDQ1 = SimdAdd(DDQ1, SimdMultiply(SimdContactTangentVAngular1, PushOutTangentV));

						DP0 = SimdAdd(DP0, DDP0);
						DQ0 = SimdAdd(DQ0, DDQ0);
						DP1 = SimdSubtract(DP1, DDP1);
						DQ1 = SimdSubtract(DQ1, DDQ1);
					}

					// Update the corrections on the bodies. 
					// NOTE: This is a scatter operation
					ScatterBodyCorrections(IsValid, DP0, DQ0, DP1, DQ1, Body0, Body1);
				}
			}

			void SolveVelocityNoFriction(
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1,
				const FSimdRealf& Dt)
			{
				// Which lanes a have a valid constraint setup?
				const FSimdSelector IsValid = SimdGreaterEqual(SimdConstraintIndex, FSimdInt32::Zero());

				// Only lanes that applied a position correction or that started with an overlap require a velocity correction
				FSimdSelector ShouldSolveVelocity = SimdOr(SimdGreater(SimdNetPushOutNormal, FSimdRealf::Zero()), SimdLess(SimdContactDeltaNormal, FSimdRealf::Zero()));
				ShouldSolveVelocity = SimdAnd(IsValid, ShouldSolveVelocity);
				if (!SimdAnyTrue(ShouldSolveVelocity))
				{
					return;
				}

				// Gather the body data we need
				FSimdVec3f V0, W0, V1, W1;
				for (int32 LaneIndex = 0; LaneIndex < TNumLanes; ++LaneIndex)
				{
					if (IsValid.GetValue(LaneIndex))
					{
						const FSolverBody* LaneBody0 = Body0.GetValue(LaneIndex);
						const FSolverBody* LaneBody1 = Body1.GetValue(LaneIndex);
						V0.SetValue(LaneIndex, LaneBody0->V());
						W0.SetValue(LaneIndex, LaneBody0->W());
						V1.SetValue(LaneIndex, LaneBody1->V());
						W1.SetValue(LaneIndex, LaneBody1->W());
					}
				}

				// Calculate the velocity error we need to correct
				const FSimdVec3f ContactVelocity0 = SimdAdd(V0, SimdCrossProduct(W0, SimdRelativeContactPoint0));
				const FSimdVec3f ContactVelocity1 = SimdAdd(V1, SimdCrossProduct(W1, SimdRelativeContactPoint1));
				const FSimdVec3f ContactVelocity = SimdSubtract(ContactVelocity0, ContactVelocity1);
				const FSimdRealf ContactVelocityNormal = SimdDotProduct(ContactVelocity, SimdContactNormal);
				FSimdRealf ContactVelocityErrorNormal = SimdSubtract(ContactVelocityNormal, SimdContactTargetVelocityNormal);

				// Make sure we don't add a correction to the lanes we should ignore
				ContactVelocityErrorNormal = SimdSelect(ShouldSolveVelocity, ContactVelocityErrorNormal, FSimdRealf::Zero());

				// Calculate the velocity correction for the error
				FSimdRealf ImpulseNormal = SimdNegate(SimdMultiply(SimdMultiply(SimdStiffness, SimdContactMassNormal), ContactVelocityErrorNormal));

				// The minimum normal impulse we can apply. We are allowed to apply a negative impulse 
				// up to an amount that would conteract the implciit velocity applied by the pushout
				// MinImpulseNormal = FMath::Min(0, -NetPushOutNormal / Dt)
				// if (NetImpulseNormal + ImpulseNormal < MinImpulseNormal) {...}
				const FSimdRealf MinImpulseNormal = SimdMin(SimdNegate(SimdDivide(SimdNetPushOutNormal, Dt)), FSimdRealf::Zero());
				const FSimdSelector ShouldClampImpulse = SimdLess(SimdAdd(SimdNetImpulseNormal, ImpulseNormal), MinImpulseNormal);
				const FSimdRealf ClampedImpulseNormal = SimdSubtract(MinImpulseNormal, SimdNetImpulseNormal);
				ImpulseNormal = SimdSelect(ShouldClampImpulse, ClampedImpulseNormal, ImpulseNormal);

				SimdNetImpulseNormal = SimdAdd(SimdNetImpulseNormal, ImpulseNormal);

				const FSimdVec3f DV0 = SimdMultiply(SimdMultiply(ImpulseNormal, SimdInvM0), SimdContactNormal);
				const FSimdVec3f DV1 = SimdMultiply(SimdMultiply(ImpulseNormal, SimdInvM1), SimdContactNormal);
				const FSimdVec3f DW0 = SimdMultiply(ImpulseNormal, SimdContactNormalAngular0);
				const FSimdVec3f DW1 = SimdMultiply(ImpulseNormal, SimdContactNormalAngular1);
				V0 = SimdAdd(V0, DV0);
				W0 = SimdAdd(W0, DW0);
				V1 = SimdSubtract(V1, DV1);
				W1 = SimdSubtract(W1, DW1);
				for (int32 LaneIndex = 0; LaneIndex < TNumLanes; ++LaneIndex)
				{
					if (IsValid.GetValue(LaneIndex))
					{
						FSolverBody* LaneBody0 = Body0.GetValue(LaneIndex);
						FSolverBody* LaneBody1 = Body1.GetValue(LaneIndex);
						LaneBody0->SetV(V0.GetValue(LaneIndex));
						LaneBody0->SetW(W0.GetValue(LaneIndex));
						LaneBody1->SetV(V1.GetValue(LaneIndex));
						LaneBody1->SetW(W1.GetValue(LaneIndex));
					}
				}
			}

			void SolveVelocityWithFriction(
				const FSimdSolverBodyPtr& Body0,
				const FSimdSolverBodyPtr& Body1,
				const FSimdRealf& Dt,
				const FSimd4Realf& FrictionStiffnessScale)
			{
				// If all the lanes have zero velocity friction, run the zero-friction path
				// (only spheres and capsules use the velocity-based dynamic friction path)
				const FSimdSelector IsValid = SimdGreaterEqual(SimdConstraintIndex, FSimdInt32::Zero());
				const FSimdSelector HasNonZeroFriction = SimdGreater(SimdVelocityFriction, FSimdRealf::Zero());
				if (!SimdAnyTrue(SimdAnd(IsValid, HasNonZeroFriction)))
				{
					SolveVelocityNoFriction(Body0, Body1, Dt);
					return;
				}

				// @todo(chaos): velocity friction
				SolveVelocityNoFriction(Body0, Body1, Dt);
			}

			FSolverVec3 GetNetPushOut(const int32 LaneIndex) const
			{
				return SimdNetPushOutNormal.GetValue(LaneIndex) * SimdContactNormal.GetValue(LaneIndex) +
					SimdNetPushOutTangentU.GetValue(LaneIndex) * SimdContactTangentU.GetValue(LaneIndex) +
					SimdNetPushOutTangentV.GetValue(LaneIndex) * SimdContactTangentV.GetValue(LaneIndex);
			}

			FSolverVec3 GetNetImpulse(const int32 LaneIndex) const
			{
				return SimdNetImpulseNormal.GetValue(LaneIndex) * SimdContactNormal.GetValue(LaneIndex) +
					SimdNetImpulseTangentU.GetValue(LaneIndex) * SimdContactTangentU.GetValue(LaneIndex) +
					SimdNetImpulseTangentV.GetValue(LaneIndex) * SimdContactTangentV.GetValue(LaneIndex);
			}

			FSolverReal GetStaticFrictionRatio(const int32 LaneIndex) const
			{
				return SimdStaticFrictionRatio.GetValue(LaneIndex);
			}

			// @todo(chaos): make private
		public:
			friend class FPBDCollisionSolverSimd;

			/**
			 * @brief Whether we need to solve velocity for this manifold point (only if we were penetrating or applied a pushout)
			*/
			bool ShouldSolveVelocity() const
			{
				return true;
			}

			FSimdInt32 SimdConstraintIndex;
			FSimdInt32 SimdManifoldPointIndex;

			FSimdRealf SimdStiffness;

			// World-space contact point relative to each particle's center of mass
			FSimdVec3f SimdRelativeContactPoint0;
			FSimdVec3f SimdRelativeContactPoint1;

			// Normal PushOut
			FSimdVec3f SimdContactNormal;
			FSimdRealf SimdContactDeltaNormal;
			FSimdRealf SimdNetPushOutNormal;
			FSimdRealf SimdContactMassNormal;
			FSimdVec3f SimdContactNormalAngular0;
			FSimdVec3f SimdContactNormalAngular1;

			// M^-1 for each body
			FSimdRealf SimdInvM0;
			FSimdRealf SimdInvM1;

			// Tangential PushOut
			FSimdVec3f SimdContactTangentU;
			FSimdVec3f SimdContactTangentV;
			FSimdRealf SimdContactDeltaTangentU;
			FSimdRealf SimdContactDeltaTangentV;
			FSimdRealf SimdNetPushOutTangentU;
			FSimdRealf SimdNetPushOutTangentV;
			FSimdRealf SimdStaticFrictionRatio;
			FSimdRealf SimdContactMassTangentU;
			FSimdRealf SimdContactMassTangentV;
			FSimdRealf SimdStaticFriction;
			FSimdRealf SimdDynamicFriction;
			FSimdRealf SimdVelocityFriction;
			FSimdVec3f SimdContactTangentUAngular0;
			FSimdVec3f SimdContactTangentVAngular0;
			FSimdVec3f SimdContactTangentUAngular1;
			FSimdVec3f SimdContactTangentVAngular1;

			// Normal Impulse
			FSimdRealf SimdContactTargetVelocityNormal;
			FSimdRealf SimdNetImpulseNormal;
			FSimdRealf SimdNetImpulseTangentU;
			FSimdRealf SimdNetImpulseTangentV;
		};

		/**
		 * @brief
		 * @todo(chaos): Make this solver operate on a single contact point rather than all points in a manifold.
		 * This would be beneficial if we have many contacts with less than 4 points in the manifold. However this
		 * is dificult to do while we are still supporting non-manifold collisions.
		*/
		class FPBDCollisionSolverSimd
		{
		public:
			static const int32 MaxConstrainedBodies = 2;
			static const int32 MaxPointsPerConstraint = 4;

			// Create a solver that is initialized to safe defaults
			static FPBDCollisionSolverSimd MakeInitialized()
			{
				FPBDCollisionSolverSimd Solver;
				Solver.State.Init();
				return Solver;
			}

			// Create a solver with no initialization
			static FPBDCollisionSolverSimd MakeUninitialized()
			{
				return FPBDCollisionSolverSimd();
			}

			// NOTE: Does not initialize any properties. See MakeInitialized
			FPBDCollisionSolverSimd() {}

			int32 GetManifoldPointBeginIndex() const
			{
				return State.ManifoldPointBeginIndex;
			}

			int32 GetLaneIndex() const
			{
				return State.LaneIndex;
			}

			/** Reset the state of the collision solver */
			void Reset()
			{
				State.SolverBody0.Reset();
				State.SolverBody1.Reset();
				State.LaneIndex = 0;
				State.ManifoldPointBeginIndex = 0;
				State.NumManifoldPoints = 0;
				State.MaxManifoldPoints = 0;
			}

			void ResetManifold()
			{
				State.NumManifoldPoints = 0;
			}

			FSolverReal StaticFriction() const { return State.StaticFriction; }
			FSolverReal DynamicFriction() const { return State.DynamicFriction; }
			FSolverReal VelocityFriction() const { return State.VelocityFriction; }

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
				State.SolverBody0.SetSolverBody(SolverBody0);
				State.SolverBody1.SetSolverBody(SolverBody1);
			}

			void SetManifoldPointsBuffer(const int32 ConstraintIndex, const int32 LaneIndex, const int32 BeginIndex, const int32 Num)
			{
				State.ConstraintIndex = ConstraintIndex;
				State.LaneIndex = LaneIndex;
				State.ManifoldPointBeginIndex = BeginIndex;
				State.NumManifoldPoints = 0;
				State.MaxManifoldPoints = Num;
			}

			int32 NumManifoldPoints() const
			{
				return State.NumManifoldPoints;
			}

			int32 AddManifoldPoint()
			{
				check(State.NumManifoldPoints < State.MaxManifoldPoints);
				return State.NumManifoldPoints++;
			}

			/**
			 * Set up a manifold point (calls InitManifodlPoint and FinalizeManifoldPoint)
			*/
			template<int TNumLanes>
			void SetManifoldPoint(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodiesBuffer,
				const int32 ManifoldPointIndex,
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
				InitManifoldPoint(
					ManifoldPointsBuffer,
					SolverBodiesBuffer,
					ManifoldPointIndex,
					InRelativeContactPosition0,
					InRelativeContactPosition1,
					InWorldContactNormal,
					InWorldContactTangentU,
					InWorldContactTangentV,
					InWorldContactDeltaNormal,
					InWorldContactDeltaTangentU,
					InWorldContactDeltaTangentV,
					InWorldContactVelocityTargetNormal);

				FinalizeManifoldPoint(
					ManifoldPointsBuffer,
					ManifoldPointIndex);
			}

			/**
			 * Initialize the contact data for a manifold point (must call FinalizeManifoldPoint before use)
			*/
			template<int TNumLanes>
			void InitManifoldPoint(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodiesBuffer,
				const int32 ManifoldPointIndex,
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
				const int32 BufferIndex = GetBufferIndex(ManifoldPointIndex);

				SolverBodiesBuffer[BufferIndex].Body0.SetValue(
					State.LaneIndex,
					&SolverBody0().SolverBody());

				SolverBodiesBuffer[BufferIndex].Body1.SetValue(
					State.LaneIndex,
					&SolverBody1().SolverBody());

				ManifoldPointsBuffer[BufferIndex].SetSharedData(
					State.LaneIndex,
					State.ConstraintIndex,
					ManifoldPointIndex);

				ManifoldPointsBuffer[BufferIndex].InitContact(
					State.LaneIndex,
					InRelativeContactPosition0,
					InRelativeContactPosition1,
					InWorldContactNormal,
					InWorldContactTangentU,
					InWorldContactTangentV,
					InWorldContactDeltaNormal,
					InWorldContactDeltaTangentU,
					InWorldContactDeltaTangentV,
					InWorldContactVelocityTargetNormal,
					State.Stiffness,
					State.StaticFriction,
					State.DynamicFriction,
					State.VelocityFriction);
			}

			/** 
			 * Finish manifold point setup.
			 * NOTE: Can only be called after the WorldContact has been set up (e.g., see SetManifoldPoint)
			*/
			template<int TNumLanes>
			void FinalizeManifoldPoint(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				const int32 ManifoldPointIndex)
			{
				ManifoldPointsBuffer[GetBufferIndex(ManifoldPointIndex)].FinalizeContact(
					State.LaneIndex,
					SolverBody0(),
					SolverBody1());
			}

			/**
			 * @brief Get the first (decorated) solver body
			 * The decorator add a possible mass scale
			*/
			FConstraintSolverBody& SolverBody0() { return State.SolverBody0; }
			const FConstraintSolverBody& SolverBody0() const { return State.SolverBody0; }

			/**
			 * @brief Get the second (decorated) solver body
			 * The decorator add a possible mass scale
			*/
			FConstraintSolverBody& SolverBody1() { return State.SolverBody1; }
			const FConstraintSolverBody& SolverBody1() const { return State.SolverBody1; }

			/**
			 * @brief Set up the mass scaling for shock propagation, using the position-phase mass scale
			*/
			void EnablePositionShockPropagation();

			/**
			 * @brief Set up the mass scaling for shock propagation, using the velocity-phase mass scale
			*/
			void EnableVelocityShockPropagation();

			/**
			 * @brief Disable mass scaling
			*/
			void DisableShockPropagation();

			template<int TNumLanes>
			FSolverVec3 GetNetPushOut(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				const int32 ManifoldPointIndex) const
			{
				return ManifoldPointsBuffer[GetBufferIndex(ManifoldPointIndex)].GetNetPushOut(State.LaneIndex);
			}

			template<int TNumLanes>
			FSolverVec3 GetNetImpulse(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				const int32 ManifoldPointIndex) const
			{
				return ManifoldPointsBuffer[GetBufferIndex(ManifoldPointIndex)].GetNetImpulse(State.LaneIndex);
			}

			template<int TNumLanes>
			FSolverReal GetStaticFrictionRatio(
				TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPointsBuffer,
				const int32 ManifoldPointIndex) const
			{
				return ManifoldPointsBuffer[GetBufferIndex(ManifoldPointIndex)].GetStaticFrictionRatio(State.LaneIndex);
			}

		private:
			int32 GetBufferIndex(const int32 ManifoldPointIndex) const
			{
				check(ManifoldPointIndex < State.NumManifoldPoints);
				return State.ManifoldPointBeginIndex + ManifoldPointIndex;
			}

			/**
			 * @brief Apply the inverse mass scale the body with the lower level
			 * @param InvMassScale
			*/
			void SetShockPropagationInvMassScale(const FSolverReal InvMassScale);

			struct FState
			{
				FState()
				{
				}

				void Init()
				{
					ConstraintIndex = INDEX_NONE;
					LaneIndex = INDEX_NONE;
					ManifoldPointBeginIndex = INDEX_NONE;
					NumManifoldPoints = 0;
					MaxManifoldPoints = 0;
					StaticFriction = 0;
					DynamicFriction = 0;
					VelocityFriction = 0;
					Stiffness = 1;
					SolverBody0.Init();
					SolverBody1.Init();
				}

				int32 ConstraintIndex;
				int32 LaneIndex;
				int32 ManifoldPointBeginIndex;
				int32 NumManifoldPoints;
				int32 MaxManifoldPoints;

				// Static Friction in the position-solve phase
				FSolverReal StaticFriction;

				// Dynamic Friction in the position-solve phase
				FSolverReal DynamicFriction;

				// Dynamic Friction in the velocity-solve phase
				FSolverReal VelocityFriction;

				// Solver stiffness (scales all pushout and impulses)
				FSolverReal Stiffness;

				// Bodies and contacts
				FConstraintSolverBody SolverBody0;
				FConstraintSolverBody SolverBody1;
			};

			FState State;
		};

		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////


		/**
		 * A helper for solving arrays of constraints.
		 * @note Only works with 4 SIMD lanes for now.
		 */
		class FPBDCollisionSolverHelperSimd
		{
		public:
			template<int TNumLanes>
			static CHAOS_API void SolvePositionNoFriction(
				const TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPoints,
				const TArrayView<TSolverBodyPtrPairSimd<TNumLanes>>& SolverBodies,
				const FSolverReal Dt,
				const FSolverReal MaxPushOut);

			template<int TNumLanes>
			static CHAOS_API void SolvePositionWithFriction(
				const TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPoints,
				const TArrayView<TSolverBodyPtrPairSimd<TNumLanes>>& SolverBodies,
				const FSolverReal Dt,
				const FSolverReal MaxPushOut);

			template<int TNumLanes>
			static CHAOS_API void SolveVelocityNoFriction(
				const TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPoints,
				const TArrayView<TSolverBodyPtrPairSimd<TNumLanes>>& SolverBodies,
				const FSolverReal Dt);

			template<int TNumLanes>
			static CHAOS_API void SolveVelocityWithFriction(
				const TArrayView<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>>& ManifoldPoints,
				const TArrayView<TSolverBodyPtrPairSimd<TNumLanes>>& SolverBodies,
				const FSolverReal Dt);

			static CHAOS_API void CheckISPC();
		};

		template<> 
		void FPBDCollisionSolverHelperSimd::SolvePositionNoFriction(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal Dt,
			const FSolverReal MaxPushOut);

		template<>
		void FPBDCollisionSolverHelperSimd::SolvePositionWithFriction(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal Dt,
			const FSolverReal MaxPushOut);

		template<>
		void FPBDCollisionSolverHelperSimd::SolveVelocityNoFriction(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal Dt);

		template<>
		void FPBDCollisionSolverHelperSimd::SolveVelocityWithFriction(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal Dt);

	}	// namespace Private
}	// namespace Chaos

