// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/Transform.h"
#include "Chaos/PBDActiveView.h"
#include "Misc/ScopeLock.h"

namespace Chaos
{
template<EGeometryParticlesSimType SimType>
class TPerParticlePBDCCDCollisionConstraint final
{
public:
	typedef TKinematicGeometryParticlesImp<FReal, 3, SimType> FCollisionParticles;

	TPerParticlePBDCCDCollisionConstraint(
		const TPBDActiveView<FCollisionParticles>& InCollisionParticlesActiveView,
		TArray<FRigidTransform3>& InCollisionTransforms,
		TArray<bool>& InCollided,
		TArray<FVec3>& InContacts,
		TArray<FVec3>& InNormals,
		TArray<uint32>& InDynamicGroupIds,
		TArray<uint32>& InKinematicGroupIds,
		const TArray<FReal>& InPerGroupThicknesses,
		const TArray<FReal>& InPerGroupFriction,
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

	inline void ApplyRange(FPBDParticles& Particles, const FReal Dt, const int32 Offset, const int32 Range) const
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
	inline void ApplyRangeHelper(FPBDParticles& Particles, const FReal Dt, const int32 Offset, const int32 Range) const
	{
		const uint32 DynamicGroupId = DynamicGroupIds[Offset];  // Particle group Id, must be the same across the entire range
		const FReal Friction = PerGroupFriction[DynamicGroupId];
		const FReal Thickness = PerGroupThicknesses[DynamicGroupId];

		PhysicsParallelFor(Range - Offset, [this, &Particles, Offset, DynamicGroupId, Thickness, Friction, Dt](int32 i)
		{
			const int32 Index = Offset + i;

			if (Particles.InvM(Index) == (FReal)0.)
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

				const FRigidTransform3 Frame(CollisionParticles.X(CollisionIndex), CollisionParticles.R(CollisionIndex));

				const Pair<FVec3, bool> PointPair = CollisionParticles.Geometry(CollisionIndex)->FindClosestIntersection(
					CollisionTransforms[CollisionIndex].InverseTransformPositionNoScale(Particles.X(Index)),
					Frame.InverseTransformPositionNoScale(Particles.P(Index)), Thickness);

				if (PointPair.Second)
				{
					Collided[CollisionIndex] = true;

					const FVec3 Normal = CollisionParticles.Geometry(CollisionIndex)->Normal(PointPair.First);
					const FVec3 NormalWorld = Frame.TransformVectorNoScale(Normal);
					const FVec3 ContactWorld = Frame.TransformPositionNoScale(PointPair.First);

					if (bLockAndWriteContacts)
					{
						check(Mutex);
						FScopeLock Lock(Mutex);
						Contacts.Emplace(ContactWorld);
						Normals.Emplace(NormalWorld);
					}
					const FVec3 Direction = ContactWorld - Particles.P(Index);
					const FReal Penetration = FMath::Max(0.f, FVec3::DotProduct(NormalWorld, Direction)) + THRESH_POINT_ON_PLANE;

					Particles.P(Index) += Penetration * NormalWorld;

					// Friction
					const FVec3 VectorToPoint = Particles.P(Index) - CollisionParticles.X(CollisionIndex);
					const FVec3 RelativeDisplacement = (Particles.P(Index) - Particles.X(Index)) - (CollisionParticles.V(CollisionIndex) + FVec3::CrossProduct(CollisionParticles.W(CollisionIndex), VectorToPoint)) * Dt;  // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
					const FVec3 RelativeDisplacementTangent = RelativeDisplacement - FVec3::DotProduct(RelativeDisplacement, NormalWorld) * NormalWorld;  // Project displacement into the tangential plane
					const FReal RelativeDisplacementTangentLength = RelativeDisplacementTangent.Size();
					if (RelativeDisplacementTangentLength >= SMALL_NUMBER)
					{
						const FReal PositionCorrection = FMath::Min<FReal>(Penetration * Friction, RelativeDisplacementTangentLength);
						const FReal CorrectionRatio = PositionCorrection / RelativeDisplacementTangentLength;
						Particles.P(Index) -= CorrectionRatio * RelativeDisplacementTangent;
					}
				}
			});
		});
	}

private:
	// TODO(mlentine): Need a bb hierarchy
	const TPBDActiveView<FCollisionParticles>& CollisionParticlesActiveView;
	const TArray<FRigidTransform3>& CollisionTransforms;
	TArray<bool>& Collided;
	TArray<FVec3>& Contacts;
	TArray<FVec3>& Normals;
	const TArray<uint32>& DynamicGroupIds;
	const TArray<uint32>& KinematicGroupIds;
	const TArray<FReal>& PerGroupThicknesses;
	const TArray<FReal>& PerGroupFriction;
	FCriticalSection* const Mutex;
};
}
