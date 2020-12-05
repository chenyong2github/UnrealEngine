// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/Transform.h"
#include "Chaos/PBDActiveView.h"
#include "Misc/ScopeLock.h"

namespace Chaos
{
template<class T, int d, EGeometryParticlesSimType SimType>
class TPerParticlePBDCCDCollisionConstraint final
{
public:
	typedef TKinematicGeometryParticlesImp<T, d, SimType> FCollisionParticles;

	TPerParticlePBDCCDCollisionConstraint(
		const TPBDActiveView<FCollisionParticles>& InCollisionParticlesActiveView,
		TArray<TRigidTransform<T, d>>& InCollisionTransforms,
		TArray<bool>& InCollided,
		TArray<TVector<T, d>>& InContacts,
		TArray<TVector<T, d>>& InNormals,
		TArray<uint32>& InDynamicGroupIds,
		TArray<uint32>& InKinematicGroupIds,
		const TArray<T>& InPerGroupThicknesses,
		const TArray<T>& InPerGroupFriction,
		bool bWriteCCDContacts)
		: CollisionParticlesActiveView(InCollisionParticlesActiveView)
		, CollisionTransforms(InCollisionTransforms)
		, Collided(InCollided)
		, Contacts(InContacts)
		, Normals(InNormals)
		, DynamicGroupIds(InDynamicGroupIds)
		, KinematicGroupIds(InKinematicGroupIds)
		, PerGroupThicknesses(InPerGroupThicknesses)
		, PerGroupFriction(InPerGroupFriction)
		, Mutex(bWriteCCDContacts ? new FCriticalSection : nullptr)
	{
	}

	~TPerParticlePBDCCDCollisionConstraint()
	{
		delete Mutex;
	}

	inline void ApplyRange(TPBDParticles<T, d>& Particles, const T Dt, const int32 Offset, const int32 Range) const
	{
		if (Mutex)
		{
			ApplyRangeHelper<true>(Particles, Dt, Offset, Range);
		}
		else
		{
			ApplyRangeHelper<false>(Particles, Dt, Offset, Range);
		}
	}

private:
	template<bool bLockAndWriteContacts>
	inline void ApplyRangeHelper(TPBDParticles<T, d>& Particles, const T Dt, const int32 Offset, const int32 Range) const
	{
		const uint32 DynamicGroupId = DynamicGroupIds[Offset];  // Particle group Id, must be the same across the entire range
		const float Friction = PerGroupFriction[DynamicGroupId];
		const float Thickness = PerGroupThicknesses[DynamicGroupId];

		PhysicsParallelFor(Range - Offset, [this, &Particles, Offset, DynamicGroupId, Thickness, Friction, Dt](int32 i)
		{
			const int32 Index = Offset + i;

			if (Particles.InvM(Index) == (T)0.)
			{
				return;  // Continue
			}

			CollisionParticlesActiveView.SequentialFor([this, &Particles, &Index, DynamicGroupId, Thickness, Friction, Dt](FCollisionParticles& CollisionParticles, int32 CollisionIndex)
			{
				const uint32 KinematicGroupId = KinematicGroupIds[CollisionIndex];  // Collision group Id

				if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
				{
					return; // Bail out if the collision groups doesn't match the particle group id, or use INDEX_NONE (= global collision that affects all particle)
				}

				const TRigidTransform<T, d> Frame(CollisionParticles.X(CollisionIndex), CollisionParticles.R(CollisionIndex));

				const Pair<TVector<T, d>, bool> PointPair = CollisionParticles.Geometry(CollisionIndex)->FindClosestIntersection(
					CollisionTransforms[CollisionIndex].InverseTransformPositionNoScale(Particles.X(Index)),
					Frame.InverseTransformPositionNoScale(Particles.P(Index)), Thickness);

				if (PointPair.Second)
				{
					Collided[CollisionIndex] = true;

					const TVector<T, d> Normal = CollisionParticles.Geometry(CollisionIndex)->Normal(PointPair.First);
					const TVector<T, d> NormalWorld = Frame.TransformVectorNoScale(Normal);
					const TVector<T, d> ContactWorld = Frame.TransformPositionNoScale(PointPair.First);

					if (bLockAndWriteContacts)
					{
						check(Mutex);
						FScopeLock Lock(Mutex);
						Contacts.Emplace(ContactWorld);
						Normals.Emplace(NormalWorld);
					}
					const TVector<T, d> Direction = ContactWorld - Particles.P(Index);
					const T Penetration = FMath::Max(0.f, TVector<T, d>::DotProduct(NormalWorld, Direction)) + THRESH_POINT_ON_PLANE;

					Particles.P(Index) += Penetration * NormalWorld;

					// Friction
					const TVector<T, d> VectorToPoint = Particles.P(Index) - CollisionParticles.X(CollisionIndex);
					const TVector<T, d> RelativeDisplacement = (Particles.P(Index) - Particles.X(Index)) - (CollisionParticles.V(CollisionIndex) + TVector<T, d>::CrossProduct(CollisionParticles.W(CollisionIndex), VectorToPoint)) * Dt;  // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
					const TVector<T, d> RelativeDisplacementTangent = RelativeDisplacement - TVector<T, d>::DotProduct(RelativeDisplacement, NormalWorld) * NormalWorld;  // Project displacement into the tangential plane
					const T RelativeDisplacementTangentLength = RelativeDisplacementTangent.Size();
					if (RelativeDisplacementTangentLength >= SMALL_NUMBER)
					{
						const T PositionCorrection = FMath::Min<T>(Penetration * Friction, RelativeDisplacementTangentLength);
						const T CorrectionRatio = PositionCorrection / RelativeDisplacementTangentLength;
						Particles.P(Index) -= CorrectionRatio * RelativeDisplacementTangent;
					}
				}
			});
		});
	}

private:
	// TODO(mlentine): Need a bb hierarchy
	const TPBDActiveView<FCollisionParticles>& CollisionParticlesActiveView;
	const TArray<TRigidTransform<T, d>>& CollisionTransforms;
	TArray<bool>& Collided;
	TArray<TVector<T, d>>& Contacts;
	TArray<TVector<T, d>>& Normals;
	const TArray<uint32>& DynamicGroupIds;
	const TArray<uint32>& KinematicGroupIds;
	const TArray<T>& PerGroupThicknesses;
	const TArray<T>& PerGroupFriction;
	FCriticalSection* const Mutex;
};
}
