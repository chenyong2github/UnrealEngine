// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Transform.h"
#include "Chaos/PBDActiveView.h"

#include "HAL/PlatformMath.h"

namespace Chaos
{
template<class T, int d, EGeometryParticlesSimType SimType>
class TPerParticlePBDCollisionConstraint : public TPerParticleRule<T, d>
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

	virtual ~TPerParticlePBDCollisionConstraint() {}

	inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) == 0)
		{
			return;
		}

		const uint32 DynamicGroupId = MDynamicGroupIds[Index];  // Particle group Id

		MCollisionParticlesActiveView.SequentialFor([this, &InParticles, &Dt, &Index, &DynamicGroupId](FCollisionParticles& CollisionParticles, int32 i)
		{
			const uint32 KinematicGroupId = MKinematicGroupIds[i];  // Collision group Id

			if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
			{
				return; // Bail out if the collision groups doesn't match the particle group id, or use INDEX_NONE (= global collision that affects all particle)
			}
			TVector<T, d> Normal;
			TRigidTransform<T, d> Frame(CollisionParticles.X(i), CollisionParticles.R(i));
			const T Phi = CollisionParticles.Geometry(i)->PhiWithNormal(Frame.InverseTransformPosition(InParticles.P(Index)), Normal);
			const T Thickness = MPerGroupThickness[DynamicGroupId];
			if (Phi < Thickness)
			{
				const TVector<T, d> NormalWorld = Frame.TransformVector(Normal);
				InParticles.P(Index) += (-Phi + Thickness) * NormalWorld;
				const T CoefficientOfFriction = MPerGroupFriction[DynamicGroupId];
				if (CoefficientOfFriction > 0)
				{
					const T MaximumFrictionCorrectionPerStep = 1.0f; // 1cm absolute maximum correction the friction force can provide
					if (bFastPositionBasedFriction)
					{
						const T Penetration = (-Phi + Thickness); // This is related to the Normal impulse
						TVector<T, d> VectorToPoint = InParticles.P(Index) - CollisionParticles.X(i);
						const TVector<T, d> RelativeDisplacement = (InParticles.P(Index) - InParticles.X(Index)) - (CollisionParticles.V(i) + TVector<T, d>::CrossProduct(CollisionParticles.W(i), VectorToPoint)) * Dt; // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
						const TVector<T, d> RelativeDisplacementTangent = RelativeDisplacement - TVector<T, d>::DotProduct(RelativeDisplacement, NormalWorld) * NormalWorld; // Project displacement into the tangential plane
						const T RelativeDisplacementTangentLength = RelativeDisplacementTangent.Size();
						T PositionCorrection = FMath::Clamp<T>(Penetration * CoefficientOfFriction, 0, RelativeDisplacementTangentLength);
						if (PositionCorrection > MaximumFrictionCorrectionPerStep)
						{
							PositionCorrection = MaximumFrictionCorrectionPerStep;
						}
						if (PositionCorrection > 0)
						{
							InParticles.P(Index) -= (PositionCorrection / RelativeDisplacementTangentLength) * RelativeDisplacementTangent;
						}
					}
					else
					{
						// Note, to fix: Only use fast position based friction for now, since adding to TMaps here is not thread safe when calling Apply on multiple threads (will cause crash)
						FVelocityConstraint Constraint;
						TVector<T, d> VectorToPoint = InParticles.P(Index) - CollisionParticles.X(i);
						Constraint.Velocity = CollisionParticles.V(i) + TVector<T, d>::CrossProduct(CollisionParticles.W(i), VectorToPoint);
						Constraint.Normal = Frame.TransformVector(Normal);
						
						MVelocityConstraints.Add(Index, Constraint);
					}
				}
			}
		});
	}

	void ApplyFriction(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
	{
		if (!bFastPositionBasedFriction)
		{
			if (!MVelocityConstraints.Contains(Index))
			{
				return;
			}
			const T VN = TVector<T, d>::DotProduct(InParticles.V(Index), MVelocityConstraints[Index].Normal);
			const T VNBody = TVector<T, d>::DotProduct(MVelocityConstraints[Index].Velocity, MVelocityConstraints[Index].Normal);
			const TVector<T, d> VTBody = MVelocityConstraints[Index].Velocity - VNBody * MVelocityConstraints[Index].Normal;
			const TVector<T, d> VTRelative = InParticles.V(Index) - VN * MVelocityConstraints[Index].Normal - VTBody;
			const T VTRelativeSize = VTRelative.Size();
			const T VNMax = FMath::Max(VN, VNBody);
			const T VNDelta = VNMax - VN;
			const T CoefficientOfFriction = MPerGroupFriction[MDynamicGroupIds[Index]];
			check(CoefficientOfFriction > 0);
			const T Friction = CoefficientOfFriction * VNDelta < VTRelativeSize ? CoefficientOfFriction * VNDelta / VTRelativeSize : 1;
			InParticles.V(Index) = VNMax * MVelocityConstraints[Index].Normal + VTBody + VTRelative * (1 - Friction);
		}
	}

  private:
	bool bFastPositionBasedFriction;
	// TODO(mlentine): Need a bb hierarchy
	const TPBDActiveView<FCollisionParticles>& MCollisionParticlesActiveView;
	TArray<bool>& MCollided;
	TArray<uint32>& MDynamicGroupIds;
	TArray<uint32>& MKinematicGroupIds;
	mutable TMap<int32, FVelocityConstraint> MVelocityConstraints;
	const TArray<T>& MPerGroupThickness;
	const TArray<T>& MPerGroupFriction;
};
}
