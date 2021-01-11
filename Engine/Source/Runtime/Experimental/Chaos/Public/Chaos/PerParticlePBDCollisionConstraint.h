// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Transform.h"
#include "Chaos/PBDActiveView.h"
#include "Chaos/GeometryParticlesfwd.h"

#include "HAL/PlatformMath.h"

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_PerParticleCollision_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_PerParticleCollision_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_PerParticleCollision_ISPC_Enabled;
#endif

namespace Chaos
{
template<class T, int d, EGeometryParticlesSimType SimType>
class CHAOS_API TPerParticlePBDCollisionConstraint final
{
	struct FVelocityConstraint
	{
		TVector<T, d> Velocity;
		TVector<T, d> Normal;
	};

public:
	typedef TKinematicGeometryParticlesImp<T, d, SimType> FCollisionParticles;

	TPerParticlePBDCollisionConstraint(const TPBDActiveView<FCollisionParticles>& InParticlesActiveView, TArray<bool>& Collided, TArray<uint32>& DynamicGroupIds, TArray<uint32>& KinematicGroupIds, const TArray<T>& PerGroupThickness, const TArray<T>& PerGroupFriction)
	    : bFastPositionBasedFriction(true), MCollisionParticlesActiveView(InParticlesActiveView), MCollided(Collided), MDynamicGroupIds(DynamicGroupIds), MKinematicGroupIds(KinematicGroupIds), MPerGroupThickness(PerGroupThickness), MPerGroupFriction(PerGroupFriction) {}

	~TPerParticlePBDCollisionConstraint() {}

	inline void ApplyRange(TPBDParticles<T, d>& Particles, const T Dt, const int32 Offset, const int32 Range) const
	{
		if (bChaos_PerParticleCollision_ISPC_Enabled && bFastPositionBasedFriction)
		{
			ApplyHelperISPC(Particles, Dt, Offset, Range);
		}
		else
		{
			ApplyHelper(Particles, Dt, Offset, Range);
		}
	}

	void ApplyFriction(TPBDParticles<T, d>& Particles, const T Dt, const int32 Index) const
	{
		check(!bFastPositionBasedFriction);  // Do not call this function if this is setup to run with fast PB friction

		if (!MVelocityConstraints.Contains(Index))
		{
			return;
		}
		const T VN = TVector<T, d>::DotProduct(Particles.V(Index), MVelocityConstraints[Index].Normal);
		const T VNBody = TVector<T, d>::DotProduct(MVelocityConstraints[Index].Velocity, MVelocityConstraints[Index].Normal);
		const TVector<T, d> VTBody = MVelocityConstraints[Index].Velocity - VNBody * MVelocityConstraints[Index].Normal;
		const TVector<T, d> VTRelative = Particles.V(Index) - VN * MVelocityConstraints[Index].Normal - VTBody;
		const T VTRelativeSize = VTRelative.Size();
		const T VNMax = FMath::Max(VN, VNBody);
		const T VNDelta = VNMax - VN;
		const T CoefficientOfFriction = MPerGroupFriction[MDynamicGroupIds[Index]];
		check(CoefficientOfFriction > 0);
		const T Friction = CoefficientOfFriction * VNDelta < VTRelativeSize ? CoefficientOfFriction * VNDelta / VTRelativeSize : 1;
		Particles.V(Index) = VNMax * MVelocityConstraints[Index].Normal + VTBody + VTRelative * (1 - Friction);
	}

private:
	inline void ApplyHelper(TPBDParticles<T, d>& Particles, const T Dt, const int32 Offset, const int32 Range) const
	{
		const uint32 DynamicGroupId = MDynamicGroupIds[Offset];  // Particle group Id, must be the same across the entire range
		const float PerGroupFriction = MPerGroupFriction[DynamicGroupId];
		const float PerGroupThickness = MPerGroupThickness[DynamicGroupId];

		if (PerGroupFriction > (T)KINDA_SMALL_NUMBER)
		{
			PhysicsParallelFor(Range - Offset, [this, &Particles, Dt, Offset, DynamicGroupId, PerGroupFriction, PerGroupThickness](int32 i)
			{
				const int32 Index = Offset + i;

				if (Particles.InvM(Index) == (T)0.)
				{
					return;  // Continue
				}

				MCollisionParticlesActiveView.SequentialFor([this, &Particles, &Dt, &Index, DynamicGroupId, PerGroupFriction, PerGroupThickness](FCollisionParticles& CollisionParticles, int32 i)
				{
					const uint32 KinematicGroupId = MKinematicGroupIds[i];  // Collision group Id

					if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
					{
						return; // Bail out if the collision groups doesn't match the particle group id, or use INDEX_NONE (= global collision that affects all particle)
					}
					TVector<T, d> Normal;
					const TRigidTransform<T, d> Frame(CollisionParticles.X(i), CollisionParticles.R(i));
					const T Phi = CollisionParticles.Geometry(i)->PhiWithNormal(Frame.InverseTransformPosition(Particles.P(Index)), Normal);

					const T Penetration = PerGroupThickness - Phi; // This is related to the Normal impulse
					if (Penetration > (T)0.)
					{
						const TVector<T, d> NormalWorld = Frame.TransformVector(Normal);
						Particles.P(Index) += Penetration * NormalWorld;

						if (bFastPositionBasedFriction)
						{
							TVector<T, d> VectorToPoint = Particles.P(Index) - CollisionParticles.X(i);
							const TVector<T, d> RelativeDisplacement = (Particles.P(Index) - Particles.X(Index)) - (CollisionParticles.V(i) + TVector<T, d>::CrossProduct(CollisionParticles.W(i), VectorToPoint)) * Dt; // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
							const TVector<T, d> RelativeDisplacementTangent = RelativeDisplacement - TVector<T, d>::DotProduct(RelativeDisplacement, NormalWorld) * NormalWorld; // Project displacement into the tangential plane
							const T RelativeDisplacementTangentLength = RelativeDisplacementTangent.Size();
							if (RelativeDisplacementTangentLength >= SMALL_NUMBER)
							{
								const T PositionCorrection = FMath::Min<T>(Penetration * PerGroupFriction, RelativeDisplacementTangentLength);
								const T CorrectionRatio = PositionCorrection / RelativeDisplacementTangentLength;
								Particles.P(Index) -= CorrectionRatio * RelativeDisplacementTangent;
							}
						}
						else
						{
							// Note, to fix: Only use fast position based friction for now, since adding to TMaps here is not thread safe when calling Apply on multiple threads (will cause crash)
							FVelocityConstraint Constraint;
							TVector<T, d> VectorToPoint = Particles.P(Index) - CollisionParticles.X(i);
							Constraint.Velocity = CollisionParticles.V(i) + TVector<T, d>::CrossProduct(CollisionParticles.W(i), VectorToPoint);
							Constraint.Normal = Frame.TransformVector(Normal);
						
							MVelocityConstraints.Add(Index, Constraint);
						}
					}
				});
			});
		}
		else
		{
			PhysicsParallelFor(Range - Offset, [this, &Particles, Dt, Offset, DynamicGroupId, PerGroupFriction, PerGroupThickness](int32 i)
			{
				const int32 Index = Offset + i;

				if (Particles.InvM(Index) == 0)
				{
					return;  // Continue
				}

				MCollisionParticlesActiveView.SequentialFor([this, &Particles, &Dt, &Index, DynamicGroupId, PerGroupFriction, PerGroupThickness](FCollisionParticles& CollisionParticles, int32 i)
				{
					const uint32 KinematicGroupId = MKinematicGroupIds[i];  // Collision group Id

					if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
					{
						return; // Bail out if the collision groups doesn't match the particle group id, or use INDEX_NONE (= global collision that affects all particle)
					}
					TVector<T, d> Normal;
					const TRigidTransform<T, d> Frame(CollisionParticles.X(i), CollisionParticles.R(i));
					const T Phi = CollisionParticles.Geometry(i)->PhiWithNormal(Frame.InverseTransformPosition(Particles.P(Index)), Normal);

					const T Penetration = PerGroupThickness - Phi; // This is related to the Normal impulse
					if (Penetration > (T)0.)
					{
						const TVector<T, d> NormalWorld = Frame.TransformVector(Normal);
						Particles.P(Index) += Penetration * NormalWorld;
					}
				});
			});
		}
	}

	void ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt, int32 Offset, int32 Range) const;

private:
	bool bFastPositionBasedFriction;
	// TODO(mlentine): Need a bb hierarchy
	const TPBDActiveView<FCollisionParticles>& MCollisionParticlesActiveView;
	TArray<bool>& MCollided;
	const TArray<uint32>& MDynamicGroupIds;
	const TArray<uint32>& MKinematicGroupIds;
	mutable TMap<int32, FVelocityConstraint> MVelocityConstraints;
	const TArray<T>& MPerGroupThickness;
	const TArray<T>& MPerGroupFriction;
};
}
