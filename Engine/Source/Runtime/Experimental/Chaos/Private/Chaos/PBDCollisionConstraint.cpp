// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintImp.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/CollisionResolutionAlgo.h"
#include "Chaos/CollisionResolutionConvexConvex.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"

#if INTEL_ISPC
#include "PBDCollisionConstraint.ispc.generated.h"
#endif

//#pragma optimize("", off)

int32 CollisionParticlesBVHDepth = 4;
FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));

int32 ConstraintBPBVHDepth = 2;
FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

int32 BPTreeOfGrids = 1;
FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));

float CollisionVelocityInflationCVar = 2.0f;
FAutoConsoleVariableRef CVarCollisionVelocityInflation(TEXT("p.CollisionVelocityInflation"), CollisionVelocityInflationCVar, TEXT("Collision velocity inflation.[def:2.0]"));

float CollisionFrictionOverride = -1.0f;
FAutoConsoleVariableRef CVarCollisionFrictionOverride(TEXT("p.CollisionFriction"), CollisionFrictionOverride, TEXT("Collision friction for all contacts if >= 0"));


extern int32 UseLevelsetCollision;

#if !UE_BUILD_SHIPPING
namespace Chaos
{
	int32 CHAOS_API PendingHierarchyDump = 0;
}
#endif

namespace Chaos
{
	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));



	//
	// Collision Constraint Container
	//

	template<typename T, int d>
	TPBDCollisionConstraint<T, d>::TPBDCollisionConstraint(
		const TPBDRigidsSOAs<T, d>& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPerParticleMaterials, 
		const int32 InApplyPairIterations /*= 1*/, 
		const int32 InApplyPushOutPairIterations /*= 1*/, 
		const T Thickness /*= (T)0*/)
		: Particles(InParticles)
		, MCollided(Collided)
		, MPhysicsMaterials(InPerParticleMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, MThickness(Thickness)
		, MAngularFriction(0)
		, bUseCCD(false)
		, bEnableCollisions(true)
		, LifespanCounter(0)
		, CollisionVelocityInflation(CollisionVelocityInflationCVar)
		, PostComputeCallback(nullptr)
		, PostApplyCallback(nullptr)
		, PostApplyPushOutCallback(nullptr)
		, SpatialAcceleration(nullptr)
	{
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::Reset"), STAT_CollisionConstraintsReset, STATGROUP_Chaos);

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_CollisionConstraintsReset);

		TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

		for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
		{
			//if (!bEnableCollisions)
			{
				RemoveConstraint(ContactHandle);
			}
		}


		MAngularFriction = 0;
		bUseCCD = false;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::RemoveConstraint(FConstraintContainerHandle* Handle)
	{
		int32 Idx = Handle->GetConstraintIndex();
		typename TCollisionConstraintBase<T, d>::FType ConstraintType = Handle->GetType();

		Handles.RemoveAtSwap(Idx);
		Constraints.RemoveAtSwap(Idx);
		if (Idx < Constraints.Num())
		{
			Handles[Idx]->SetConstraintIndex(Idx, ConstraintType);
		}

		ensure(Handles.Num() == Constraints.Num());
		HandleAllocator.FreeHandle(Handle);
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  InHandleSet)
	{
		const TArray<TGeometryParticleHandle<T, d>*> HandleArray = InHandleSet.Array();
		for (auto ParticleHandle : HandleArray)
		{
			TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

			for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
			{
				TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles = ContactHandle->GetConstrainedParticles();
				if (ConstraintParticles[1] == ParticleHandle || ConstraintParticles[0] == ParticleHandle)
				{
					RemoveConstraint(ContactHandle);
				}
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ApplyCollisionModifier(const TFunction<ECollisionModifierResult(const FConstraintContainerHandle* Handle)>& CollisionModifier)
	{
		TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

		for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
		{
			ECollisionModifierResult Result = CollisionModifier(ContactHandle);
			if (Result == ECollisionModifierResult::Disabled)
			{
				RemoveConstraint(ContactHandle);
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::SetPostComputeCallback(const TRigidBodyContactConstraintsPostComputeCallback<T, d>& Callback)
	{
		PostComputeCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostComputeCallback()
	{
		PostComputeCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::SetPostApplyCallback(const TRigidBodyContactConstraintsPostApplyCallback<T, d>& Callback)
	{
		PostApplyCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::SetPostApplyPushOutCallback(const TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d>& Callback)
	{
		PostApplyPushOutCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostApplyPushOutCallback()
	{
		PostApplyPushOutCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt)
	{
		Reset();

		if (bEnableCollisions)
		{
#if !UE_BUILD_SHIPPING
			if (PendingHierarchyDump)
			{
				ComputeConstraints<true>(*SpatialAcceleration, Dt);
			}
			else
#endif
			{
				ComputeConstraints(*SpatialAcceleration, Dt);
			}
		}
	}

	DEFINE_STAT(STAT_ComputeConstraints);
	DEFINE_STAT(STAT_ComputeConstraintsNP);
	DEFINE_STAT(STAT_ComputeConstraintsBP);
	DEFINE_STAT(STAT_ComputeConstraintsSU);

	template<typename T, int d>
	template <bool bGatherStats>
	void TPBDCollisionConstraint<T, d>::ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt)
	{
		if (const auto AABBTree = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTree, Dt);
		}
		else if (const auto BV = AccelerationStructure.template As<TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*BV, Dt);
		}
		else if (const auto AABBTreeBV = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTreeBV, Dt);
		}
		else if (const auto Collection = AccelerationStructure.template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>())
		{
			if (bGatherStats)
			{
				Collection->PBDComputeConstraintsLowLevel_GatherStats(*this, Dt);
			}
			else
			{
				Collection->PBDComputeConstraintsLowLevel(*this, Dt);
			}
		}
		else
		{
			check(false);  //question: do we want to support a dynamic dispatch version?
		}

		if (PostComputeCallback != nullptr)
		{
			PostComputeCallback();
		}
	}



	// @todo(ccaulfield): This is duplicated in JointConstraints - move to a utility file
	template<typename T>
	PMatrix<T, 3, 3> ComputeFactorMatrix3(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
	{
		// Rigid objects rotational contribution to the impulse.
		// Vx*M*VxT+Im
		check(Im > FLT_MIN)
			return PMatrix<T, 3, 3>(
				-V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
				V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
				-V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
				V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
				-V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
				-V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
	}

	template<typename T, int d>
	TVector<T, d> GetEnergyClampedImpulse(const TRigidBodySingleContactConstraint<T, d>& Constraint, const TVector<T, d>& Impulse, const TVector<T, d>& VectorToPoint1, const TVector<T, d>& VectorToPoint2, const TVector<T, d>& Velocity1, const TVector<T, d>& Velocity2)
	{
		TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint.Particle[0]->AsDynamic();
		TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint.Particle[1]->AsDynamic();

		TVector<T, d> Jr0, Jr1, IInvJr0, IInvJr1;
		T ImpulseRatioNumerator0 = 0, ImpulseRatioNumerator1 = 0, ImpulseRatioDenom0 = 0, ImpulseRatioDenom1 = 0;
		T ImpulseSize = Impulse.SizeSquared();
		TVector<T, d> KinematicVelocity = !PBDRigid0 ? Velocity1 : !PBDRigid1 ? Velocity2 : TVector<T, d>(0);
		if (PBDRigid0)
		{
			Jr0 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
			IInvJr0 = PBDRigid0->Q().RotateVector(PBDRigid0->InvI() * PBDRigid0->Q().UnrotateVector(Jr0));
			ImpulseRatioNumerator0 = TVector<T, d>::DotProduct(Impulse, PBDRigid0->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr0, PBDRigid0->W());
			ImpulseRatioDenom0 = ImpulseSize / PBDRigid0->M() + TVector<T, d>::DotProduct(Jr0, IInvJr0);
		}
		if (PBDRigid1)
		{
			Jr1 = TVector<T, d>::CrossProduct(VectorToPoint2, Impulse);
			IInvJr1 = PBDRigid1->Q().RotateVector(PBDRigid1->InvI() * PBDRigid1->Q().UnrotateVector(Jr1));
			ImpulseRatioNumerator1 = TVector<T, d>::DotProduct(Impulse, PBDRigid1->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr1, PBDRigid1->W());
			ImpulseRatioDenom1 = ImpulseSize / PBDRigid1->M() + TVector<T, d>::DotProduct(Jr1, IInvJr1);
		}
		T Numerator = -2 * (ImpulseRatioNumerator0 - ImpulseRatioNumerator1);
		if (Numerator < 0)
		{
			return TVector<T, d>(0);
		}
		check(Numerator >= 0);
		T Denominator = ImpulseRatioDenom0 + ImpulseRatioDenom1;
		return Numerator < Denominator ? (Impulse * Numerator / Denominator) : Impulse;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}


	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Apply(const T Dt, FRigidBodyContactConstraint& Constraint, const int32 It, const int32 NumIts)
	{
		TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(Constraint.Particle[0]);
		TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(Constraint.Particle[1]);
		TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
		TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

		// @todo(ccaulfield): I think we should never get this? Revisit after particle handle refactor
		if (Particle0->Sleeping())
		{
			ensure(!PBDRigid1 || PBDRigid1->Sleeping());
			return;
		}
		if (Particle1->Sleeping())
		{
			ensure(!PBDRigid0 || PBDRigid0->Sleeping());
			return;
		}

		for (int32 PairIt = 0; PairIt < MApplyPairIterations; ++PairIt)
		{
			UpdateConstraint<ECollisionUpdateType::Deepest>(MThickness, Constraint);
			if (Constraint.GetPhi() >= MThickness)
			{
				return;
			}

			// @todo(ccaulfield): CHAOS_PARTICLEHANDLE_TODO what's the best way to manage external per-particle data?
			Particle0->AuxilaryValue(MCollided) = true;
			Particle1->AuxilaryValue(MCollided) = true;

			// @todo(ccaulfield): CHAOS_PARTICLEHANDLE_TODO split function to avoid ifs
			const TVector<T, d> ZeroVector = TVector<T, d>(0);
			const TRotation<T, d>& Q0 = Particle0->Q();
			const TRotation<T, d>& Q1 = Particle1->Q();
			const TVector<T, d>& P0 = Particle0->P();
			const TVector<T, d>& P1 = Particle1->P();
			const TVector<T, d>& V0 = Particle0->V();
			const TVector<T, d>& V1 = Particle1->V();
			const TVector<T, d>& W0 = Particle0->W();
			const TVector<T, d>& W1 = Particle1->W();
			TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial0 = Particle0->AuxilaryValue(MPhysicsMaterials);
			TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial1 = Particle1->AuxilaryValue(MPhysicsMaterials);

			TConvexManifold<T, d> & Contact = Constraint.ShapeManifold;

			TVector<T, d> VectorToPoint1 = Contact.Location - P0;
			TVector<T, d> VectorToPoint2 = Contact.Location - P1;
			TVector<T, d> Body1Velocity = V0 + TVector<T, d>::CrossProduct(W0, VectorToPoint1);
			TVector<T, d> Body2Velocity = V1 + TVector<T, d>::CrossProduct(W1, VectorToPoint2);
			TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
			T RelativeNormalVelocity = TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal);

			if (RelativeNormalVelocity < 0) // ignore separating constraints
			{
				PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0 ? (Q0 * FMatrix::Identity).GetTransposed() * PBDRigid0->InvI() * (Q0 * FMatrix::Identity) : PMatrix<T, d, d>(0);
				PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (Q1 * FMatrix::Identity).GetTransposed() * PBDRigid1->InvI() * (Q1 * FMatrix::Identity) : PMatrix<T, d, d>(0);
				PMatrix<T, d, d> Factor =
					(PBDRigid0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
					(PBDRigid1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
				TVector<T, d> Impulse;
				TVector<T, d> AngularImpulse(0);

				// Resting contact if very close to the surface
				T Restitution = (T)0;
				T Friction = (T)0;
				bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * Dt));
				if (PhysicsMaterial0 && PhysicsMaterial1)
				{
					if (bApplyRestitution)
					{
						Restitution = FMath::Min(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution);
					}
					Friction = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial1->Friction);
				}
				else if (PhysicsMaterial0)
				{
					if (bApplyRestitution)
					{
						Restitution = PhysicsMaterial0->Restitution;
					}
					Friction = PhysicsMaterial0->Friction;
				}
				else if (PhysicsMaterial1)
				{
					if (bApplyRestitution)
					{
						Restitution = PhysicsMaterial1->Restitution;
					}
					Friction = PhysicsMaterial1->Friction;
				}

				if (CollisionFrictionOverride >= 0)
				{
					Friction = CollisionFrictionOverride;
				}

				if (Friction)
				{
					if (RelativeNormalVelocity > 0)
					{
						RelativeNormalVelocity = 0;
					}
					TVector<T, d> VelocityChange = -(Restitution * RelativeNormalVelocity * Contact.Normal + RelativeVelocity);
					T NormalVelocityChange = TVector<T, d>::DotProduct(VelocityChange, Contact.Normal);
					PMatrix<T, d, d> FactorInverse = Factor.Inverse();
					TVector<T, d> MinimalImpulse = FactorInverse * VelocityChange;
					const T MinimalImpulseDotNormal = TVector<T, d>::DotProduct(MinimalImpulse, Contact.Normal);
					const T TangentialSize = (MinimalImpulse - MinimalImpulseDotNormal * Contact.Normal).Size();
					if (TangentialSize <= Friction * MinimalImpulseDotNormal)
					{
						//within friction cone so just solve for static friction stopping the object
						Impulse = MinimalImpulse;
						if (MAngularFriction)
						{
							TVector<T, d> RelativeAngularVelocity = W0 - W1;
							T AngularNormal = TVector<T, d>::DotProduct(RelativeAngularVelocity, Contact.Normal);
							TVector<T, d> AngularTangent = RelativeAngularVelocity - AngularNormal * Contact.Normal;
							TVector<T, d> FinalAngularVelocity = FMath::Sign(AngularNormal) * FMath::Max((T)0, FMath::Abs(AngularNormal) - MAngularFriction * NormalVelocityChange) * Contact.Normal + FMath::Max((T)0, AngularTangent.Size() - MAngularFriction * NormalVelocityChange) * AngularTangent.GetSafeNormal();
							TVector<T, d> Delta = FinalAngularVelocity - RelativeAngularVelocity;
							if (!PBDRigid0 && PBDRigid1)
							{
								PMatrix<T, d, d> WorldSpaceI2 = (Q1 * FMatrix::Identity) * PBDRigid1->I() * (Q1 * FMatrix::Identity).GetTransposed();
								TVector<T, d> ImpulseDelta = PBDRigid1->M() * TVector<T, d>::CrossProduct(VectorToPoint2, Delta);
								Impulse += ImpulseDelta;
								AngularImpulse += WorldSpaceI2 * Delta - TVector<T, d>::CrossProduct(VectorToPoint2, ImpulseDelta);
							}
							else if (PBDRigid0 && !PBDRigid1)
							{
								PMatrix<T, d, d> WorldSpaceI1 = (Q0 * FMatrix::Identity) * PBDRigid0->I() * (Q0 * FMatrix::Identity).GetTransposed();
								TVector<T, d> ImpulseDelta = PBDRigid0->M() * TVector<T, d>::CrossProduct(VectorToPoint1, Delta);
								Impulse += ImpulseDelta;
								AngularImpulse += WorldSpaceI1 * Delta - TVector<T, d>::CrossProduct(VectorToPoint1, ImpulseDelta);
							}
							else if (PBDRigid0 && PBDRigid1)
							{
								PMatrix<T, d, d> Cross1(0, VectorToPoint1.Z, -VectorToPoint1.Y, -VectorToPoint1.Z, 0, VectorToPoint1.X, VectorToPoint1.Y, -VectorToPoint1.X, 0);
								PMatrix<T, d, d> Cross2(0, VectorToPoint2.Z, -VectorToPoint2.Y, -VectorToPoint2.Z, 0, VectorToPoint2.X, VectorToPoint2.Y, -VectorToPoint2.X, 0);
								PMatrix<T, d, d> CrossI1 = Cross1 * WorldSpaceInvI1;
								PMatrix<T, d, d> CrossI2 = Cross2 * WorldSpaceInvI2;
								PMatrix<T, d, d> Diag1 = CrossI1 * Cross1.GetTransposed() + CrossI2 * Cross2.GetTransposed();
								Diag1.M[0][0] += PBDRigid0->InvM() + PBDRigid1->InvM();
								Diag1.M[1][1] += PBDRigid0->InvM() + PBDRigid1->InvM();
								Diag1.M[2][2] += PBDRigid0->InvM() + PBDRigid1->InvM();
								PMatrix<T, d, d> OffDiag1 = (CrossI1 + CrossI2) * -1;
								PMatrix<T, d, d> Diag2 = (WorldSpaceInvI1 + WorldSpaceInvI2).Inverse();
								PMatrix<T, d, d> OffDiag1Diag2 = OffDiag1 * Diag2;
								TVector<T, d> ImpulseDelta = PMatrix<T, d, d>((Diag1 - OffDiag1Diag2 * OffDiag1.GetTransposed()).Inverse())* ((OffDiag1Diag2 * -1) * Delta);
								Impulse += ImpulseDelta;
								AngularImpulse += Diag2 * (Delta - PMatrix<T, d, d>(OffDiag1.GetTransposed()) * ImpulseDelta);
							}
						}
					}
					else
					{
						//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
						TVector<T, d> Tangent = (RelativeVelocity - TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal).GetSafeNormal();
						TVector<T, d> DirectionalFactor = Factor * (Contact.Normal - Friction * Tangent);
						T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, DirectionalFactor);
						if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
							*Constraint.ToString(),
							*Particle0->ToString(),
							*Particle1->ToString(),
							*DirectionalFactor.ToString(), ImpulseDenominator))
						{
							ImpulseDenominator = (T)1;
						}

						const T ImpulseMag = -(1 + Restitution) * RelativeNormalVelocity / ImpulseDenominator;
						Impulse = ImpulseMag * (Contact.Normal - Friction * Tangent);
					}
				}
				else
				{
					T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, Factor * Contact.Normal);
					TVector<T, d> ImpulseNumerator = -(1 + Restitution) * TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal)* Contact.Normal;
					if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
						*Constraint.ToString(),
						*Particle0->ToString(),
						*Particle1->ToString(),
						*(Factor * Contact.Normal).ToString(), ImpulseDenominator))
					{
						ImpulseDenominator = (T)1;
					}
					Impulse = ImpulseNumerator / ImpulseDenominator;
				}
				Impulse = GetEnergyClampedImpulse(Constraint, Impulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
				Constraint.AccumulatedImpulse += Impulse;
				if (PBDRigid0)
				{
					// Velocity update for next step
					FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint1, Impulse) + AngularImpulse;
					FVec3 DV = PBDRigid0->InvM() * Impulse;
					FVec3 DW = WorldSpaceInvI1 * NetAngularImpulse;
					PBDRigid0->V() += DV;
					PBDRigid0->W() += DW;
					// Position update as part of pbd
					PBDRigid0->P() += DV * Dt;
					PBDRigid0->Q() += FRotation3::FromElements(DW, 0.f) * Q0 * Dt * T(0.5);
					PBDRigid0->Q().Normalize();
				}
				if (PBDRigid1)
				{
					// Velocity update for next step
					FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint2, -Impulse) - AngularImpulse;
					FVec3 DV = -PBDRigid1->InvM() * Impulse;
					FVec3 DW = WorldSpaceInvI2 * NetAngularImpulse;
					PBDRigid1->V() += DV;
					PBDRigid1->W() += DW;
					// Position update as part of pbd
					PBDRigid1->P() += DV * Dt;
					PBDRigid1->Q() += FRotation3::FromElements(DW, 0.f) * Q1 * Dt * T(0.5);
					PBDRigid1->Q().Normalize();
				}
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::Apply"), STAT_Apply, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Apply);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);
				Apply(Dt, Constraints[ConstraintHandle->GetConstraintIndex()], It, NumIts);
			}, bDisableCollisionParallelFor);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, InConstraintHandles);
		}
	}


	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool &NeedsAnotherIteration)
	{
		TGeometryParticleHandle<T, d>* Particle0 = Constraint.Particle[0];
		TGeometryParticleHandle<T, d>* Particle1 = Constraint.Particle[1];
		TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
		TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

		// @todo(ccaulfield): I think we should never get this? Revisit after particle handle refactor
		if (Particle0->Sleeping())
		{
			ensure(!PBDRigid1 || PBDRigid1->Sleeping());
			return;
		}
		if (Particle1->Sleeping())
		{
			ensure(!PBDRigid0 || PBDRigid0->Sleeping());
			return;
		}

		const TVector<T, d> ZeroVector = TVector<T, d>(0);
		const TRotation<T, d>& Q0 = PBDRigid0 ? PBDRigid0->Q() : Particle0->R();
		const TRotation<T, d>& Q1 = PBDRigid1 ? PBDRigid1->Q() : Particle1->R();
		const TVector<T, d>& P0 = PBDRigid0 ? PBDRigid0->P() : Particle0->X();
		const TVector<T, d>& P1 = PBDRigid1 ? PBDRigid1->P() : Particle1->X();
		const TVector<T, d>& V0 = PBDRigid0 ? PBDRigid0->V() : ZeroVector;
		const TVector<T, d>& V1 = PBDRigid1 ? PBDRigid1->V() : ZeroVector;
		const TVector<T, d>& W0 = PBDRigid0 ? PBDRigid0->W() : ZeroVector;
		const TVector<T, d>& W1 = PBDRigid1 ? PBDRigid1->W() : ZeroVector;
		TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial0 = Particle0->AuxilaryValue(MPhysicsMaterials);
		TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial1 = Particle1->AuxilaryValue(MPhysicsMaterials);
		const bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0);
		const bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1);

		for (int32 PairIteration = 0; PairIteration < MApplyPushOutPairIterations; ++PairIteration)
		{
			UpdateConstraint<ECollisionUpdateType::Deepest>(MThickness, Constraint);

			const TConvexManifold<T, d> & Contact = Constraint.ShapeManifold;

			if (Contact.Phi >= MThickness)
			{
				break;
			}

			if ((!PBDRigid0 || IsTemporarilyStatic0) && (!PBDRigid1 || IsTemporarilyStatic1))
			{
				break;
			}

			NeedsAnotherIteration = true;
			PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0 ? (Q0 * FMatrix::Identity).GetTransposed() * PBDRigid0->InvI() * (Q0 * FMatrix::Identity) : PMatrix<T, d, d>(0);
			PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (Q1 * FMatrix::Identity).GetTransposed() * PBDRigid1->InvI() * (Q1 * FMatrix::Identity) : PMatrix<T, d, d>(0);
			TVector<T, d> VectorToPoint1 = Contact.Location - P0;
			TVector<T, d> VectorToPoint2 = Contact.Location - P1;
			PMatrix<T, d, d> Factor =
				(PBDRigid0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
				(PBDRigid1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
			T Numerator = FMath::Min((T)(Iteration + 2), (T)NumIterations);
			T ScalingFactor = Numerator / (T)NumIterations;

			//if pushout is needed we better fix relative velocity along normal. Treat it as if 0 restitution
			TVector<T, d> Body1Velocity = V0 + TVector<T, d>::CrossProduct(W0, VectorToPoint1);
			TVector<T, d> Body2Velocity = V1 + TVector<T, d>::CrossProduct(W1, VectorToPoint2);
			TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
			const T RelativeVelocityDotNormal = TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal);
			if (RelativeVelocityDotNormal < 0)
			{
				T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, Factor * Contact.Normal);
				TVector<T, d> ImpulseNumerator = -TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal * ScalingFactor;
				if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("ApplyPushout Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Contact.Normal:%s, ImpulseDenominator:%f"),
					*Constraint.ToString(),
					*Particle0->ToString(),
					*Particle1->ToString(),
					*(Factor*Contact.Normal).ToString(), ImpulseDenominator))
				{
					ImpulseDenominator = (T)1;
				}

				TVector<T, d> VelocityFixImpulse = ImpulseNumerator / ImpulseDenominator;
				VelocityFixImpulse = GetEnergyClampedImpulse(Constraint, VelocityFixImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
				Constraint.AccumulatedImpulse += VelocityFixImpulse;	//question: should we track this?
				if (!IsTemporarilyStatic0 && PBDRigid0)
				{
					TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint1, VelocityFixImpulse);
					PBDRigid0->V() += PBDRigid0->InvM() * VelocityFixImpulse;
					PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse;

				}

				if (!IsTemporarilyStatic1 && PBDRigid1)
				{
					TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
					PBDRigid1->V() -= PBDRigid1->InvM() * VelocityFixImpulse;
					PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse;
				}

			}


			TVector<T, d> Impulse = PMatrix<T, d, d>(Factor.Inverse()) * ((-Contact.Phi + MThickness) * ScalingFactor * Contact.Normal);
			TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
			TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse);
			if (!IsTemporarilyStatic0 && PBDRigid0)
			{
				PBDRigid0->P() += PBDRigid0->InvM() * Impulse;
				PBDRigid0->Q() = TRotation<T, d>::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
				PBDRigid0->Q().Normalize();
			}
			if (!IsTemporarilyStatic1 && PBDRigid1)
			{
				PBDRigid1->P() -= PBDRigid1->InvM() * Impulse;
				PBDRigid1->Q() = TRotation<T, d>::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
				PBDRigid1->Q().Normalize();
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::ApplyPushOut"), STAT_ApplyPushOut, STATGROUP_Chaos);
	template<typename T, int d>
	bool TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut);

		bool NeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);
				ApplyPushOut(Dt, Constraints[ConstraintHandle->GetConstraintIndex()], IsTemporarilyStatic, Iteration, NumIterations, NeedsAnotherIteration);
			}, bDisableCollisionParallelFor);
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, InConstraintHandles, NeedsAnotherIteration);
		}
		return NeedsAnotherIteration;
	}


	template<typename T, int d>
	TRigidTransform<T, d> GetTransform(const TGeometryParticleHandle<T, d>* Particle)
	{
		TGenericParticleHandle<T, d> Generic = const_cast<TGeometryParticleHandle<T, d>*>(Particle);	//TODO: give a const version of the generic API
		return TRigidTransform<T, d>(Generic->P(), Generic->Q());
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness, TRigidBodySingleContactConstraint<T, d> & Constraint)
	{
		if (ensure(Particle0 && Particle1))
		{
			ConstructConstraintsImpl<T, d>(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Thickness, Constraint);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateConstraint"), STAT_UpdateConstraint, STATGROUP_ChaosWide);

	template<typename T, int d>
	template<ECollisionUpdateType UpdateType>
	void TPBDCollisionConstraint<T, d>::UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConstraint);

		Constraint.ResetPhi(Thickness);
		const TRigidTransform<T, d> ParticleTM = GetTransform(Constraint.Particle[0]);
		const TRigidTransform<T, d> LevelsetTM = GetTransform(Constraint.Particle[1]);

		if (!Constraint.Particle[0]->Geometry())
		{
			if (Constraint.Particle[1]->Geometry())
			{
				if (!Constraint.Particle[1]->Geometry()->IsUnderlyingUnion())
				{
					UpdateLevelsetConstraint<UpdateType>(Thickness, Constraint);
				}
				else
				{
					UpdateUnionLevelsetConstraint<UpdateType>(Thickness, Constraint);
				}
			}
		}
		else
		{
			UpdateConstraintImp<UpdateType>(*Constraint.Particle[0]->Geometry(), ParticleTM, *Constraint.Particle[1]->Geometry(), LevelsetTM, Thickness, Constraint);
		}
	}

	template class TPBDCollisionConstraintHandle<float, 3>;
	template class TAccelerationStructureHandle<float, 3>;
	template class CHAOS_API TPBDCollisionConstraint<float, 3>;
	template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
	template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
	template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
	template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
}
