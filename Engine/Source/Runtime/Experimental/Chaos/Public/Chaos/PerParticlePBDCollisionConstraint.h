// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Transform.h"

#include <memory>

namespace Chaos
{
template<class T, int d, EGeometryParticlesSimType SimType>
class TPerParticlePBDCollisionConstraint : public TPerParticleRule<T, d>
{
	struct VelocityConstraint
	{
		TVector<T, d> Velocity;
		TVector<T, d> Normal;
	};

  public:
	TPerParticlePBDCollisionConstraint(const TKinematicGeometryParticlesImp<T, d, SimType>& InParticles, TArrayCollectionArray<bool>& Collided, TArrayCollectionArray<uint32>& DynamicGroupIds, TArrayCollectionArray<uint32>& KinematicGroupIds, const TArray<T>& PerGroupThickness, const TArray<T>& PerGroupFriction)
	    : bFastPositionBasedFriction(true), MParticles(InParticles), MCollided(Collided), MDynamicGroupIds(DynamicGroupIds), MKinematicGroupIds(KinematicGroupIds), MPerGroupThickness(PerGroupThickness), MPerGroupFriction(PerGroupFriction) {}
	virtual ~TPerParticlePBDCollisionConstraint() {}

	inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) == 0)
			return;
		for (uint32 i = 0; i < MParticles.Size(); ++i)
		{
			if (MDynamicGroupIds[Index] != MKinematicGroupIds[i])
			{
				continue; // Bail out if the collision groups doesn't match the particle group id
			}
			TVector<T, d> Normal;
			TRigidTransform<T, d> Frame(MParticles.X(i), MParticles.R(i));
			const T Phi = MParticles.Geometry(i)->PhiWithNormal(Frame.InverseTransformPosition(InParticles.P(Index)), Normal);
			const T Thickness = MPerGroupThickness[MDynamicGroupIds[Index]];
			if (Phi < Thickness)
			{
				const TVector<T, d> NormalWorld = Frame.TransformVector(Normal);
				InParticles.P(Index) += (-Phi + Thickness) * NormalWorld;
				const T CoefficientOfFriction = MPerGroupFriction[MDynamicGroupIds[Index]];
				if (CoefficientOfFriction > 0)
				{
					const T MaximumFrictionCorrectionPerStep = 1.0f; // 1cm absolute maximum correction the friction force can provide
					if (bFastPositionBasedFriction)
					{
						const T Penetration = (-Phi + Thickness); // This is related to the Normal impulse
						TVector<T, d> VectorToPoint = InParticles.P(Index) - MParticles.X(i);
						const TVector<T, d> RelativeDisplacement = (InParticles.P(Index) - InParticles.X(Index)) - (MParticles.V(i) + TVector<T, d>::CrossProduct(MParticles.W(i), VectorToPoint)) * Dt; // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
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
						VelocityConstraint Constraint;
						TVector<T, d> VectorToPoint = InParticles.P(Index) - MParticles.X(i);
						Constraint.Velocity = MParticles.V(i) + TVector<T, d>::CrossProduct(MParticles.W(i), VectorToPoint);
						Constraint.Normal = Frame.TransformVector(Normal);
						
						MVelocityConstraints.Add(Index, Constraint);
					}
				}
			}
		}
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
			const T VNMax = FGenericPlatformMath::Max(VN, VNBody);
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
	const TKinematicGeometryParticlesImp<T, d, SimType>& MParticles;
	TArrayCollectionArray<bool>& MCollided;
	TArrayCollectionArray<uint32>& MDynamicGroupIds;
	TArrayCollectionArray<uint32>& MKinematicGroupIds;
	mutable TMap<int32, VelocityConstraint> MVelocityConstraints;
	const TArray<T>& MPerGroupThickness;
	const TArray<T>& MPerGroupFriction;
};
}
