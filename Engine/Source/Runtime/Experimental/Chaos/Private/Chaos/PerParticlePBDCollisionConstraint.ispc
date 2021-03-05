// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Transform.isph"

enum ImplicitObjectType
{
	//Note: add entries in order to avoid serialization issues (but before IsInstanced)
	Sphere = 0,
	Box,
	Plane,
	Capsule,
	Transformed,
	Union,
	LevelSet,
	Unknown,
	Convex,
	TaperedCylinder,
	Cylinder,
	TriangleMesh,
	HeightField,
	DEPRECATED_Scaled,	//needed for serialization of existing data
	Triangle,
	UnionClustered,
	TaperedCapsule,

	//Add entries above this line for serialization
	IsInstanced = 1 << 6,
	IsScaled = 1 << 7
};

struct SerializedPtr
{
	uint8 *Ptr;
};

struct UniquePtr
{
	SerializedPtr *Data;
};

struct Segment
{
	FVector Point;
	FVector Axis;
	float Length;
};

struct PlaneConcrete
{
	FVector MX;
	FVector MNormal;
};

struct Plane
{
	PlaneConcrete MPlaneConcrete;
};

static inline float SafeNormalize(FVector &Direction)
{
	const float SizeSqr = VectorSizeSquared(Direction);
	const float Size = sqrt(SizeSqr);
	Direction = VectorSelect((SizeSqr < KINDA_SMALL_NUMBER), ForwardVector, Direction / Size);
	return (Size < KINDA_SMALL_NUMBER) ? 0.f : Size;
}

static inline bool VectorNormalize(FVector &V)
{
	const float SquareSum = VectorSizeSquared(V);
	if(SquareSum > SMALL_NUMBER)
	{
		const float Scale = InvSqrt(SquareSum);
		V = V * Scale;
		return true;
	}
	return false;
}

static inline void PhiWithNormalSphere(const uniform uint8 *uniform TypedObjectPtr, const FVector &PhiWithNormalInput, FVector &Normal, float &Phi, const uniform float Radius)
{
	const uniform FVector Center = *((const uniform FVector *uniform)&TypedObjectPtr[0]);
	Normal = PhiWithNormalInput - Center;
	Phi = SafeNormalize(Normal) - Radius;
}

static inline void PhiWithNormalCapsule(const uniform uint8 *uniform TypedObjectPtr, const FVector &PhiWithNormalInput, FVector &Normal, float &Phi, const uniform float Radius)
{
	const uniform Segment MSegment = *((const uniform Segment *uniform)&TypedObjectPtr[0]);

	const float Dot = clamp(VectorDot(PhiWithNormalInput - MSegment.Point, MSegment.Axis), 0.0f, MSegment.Length);
	const FVector ProjectedPoint = SetVector(Dot) * MSegment.Axis + MSegment.Point;
	Normal = PhiWithNormalInput - ProjectedPoint;
	Phi = SafeNormalize(Normal) - Radius;
}

static inline void PhiWithNormalTaperedCapsule(const uniform uint8* uniform TypedObjectPtr, const FVector& PhiWithNormalInput, FVector& Normal, float& Phi)
{
	const uniform FVector Origin = *((const uniform FVector *uniform)& TypedObjectPtr[0]);
	const uniform FVector Axis   = *((const uniform FVector *uniform)& TypedObjectPtr[sizeof(uniform FVector)]);
	const uniform float Height   = *((const uniform float *uniform)& TypedObjectPtr[2*sizeof(uniform FVector)]);
	const uniform float Radius1  = *((const uniform float *uniform)& TypedObjectPtr[2*sizeof(uniform FVector)+ sizeof(uniform float)]);
	const uniform float Radius2  = *((const uniform float *uniform)& TypedObjectPtr[2*sizeof(uniform FVector)+ 2*sizeof(uniform float)]);

	const float DistanceAlongAxis = clamp(VectorDot(PhiWithNormalInput - Origin, Axis), 0.0f, Height);
	const FVector ProjectedPoint = SetVector(DistanceAlongAxis) * Axis + Origin;
	const float Alpha = DistanceAlongAxis / Height;
	const float Radius = Radius1 * (1.0f - Alpha) + Radius2 * Alpha;
	Normal = PhiWithNormalInput - ProjectedPoint;
	Phi = SafeNormalize(Normal) - Radius;
}

static inline void PhiWithNormalTaperedCylinder(const uniform uint8 *uniform TypedObjectPtr, const FVector &PhiWithNormalInput, FVector &Normal, float &Phi, const uniform int SizeofFImplicitObject)
{
	const uniform Plane MPlane1 = *((const uniform Plane *uniform)&TypedObjectPtr[SizeofFImplicitObject]);
	const uniform Plane MPlane2 = *((const uniform Plane *uniform)&TypedObjectPtr[2*SizeofFImplicitObject+sizeof(uniform Plane)]);
	const uniform float MHeight = *((const uniform float *uniform)&TypedObjectPtr[2*SizeofFImplicitObject+2*sizeof(uniform Plane)]);
	const uniform float MRadius1 = *((const uniform float *uniform)&TypedObjectPtr[2*SizeofFImplicitObject + 2*sizeof(uniform Plane) + sizeof(uniform float)]);
	const uniform float MRadius2 = *((const uniform float *uniform)&TypedObjectPtr[2*SizeofFImplicitObject + 2*sizeof(uniform Plane) + 2*sizeof(uniform float)]);

	const float Distance1 = VectorDot(PhiWithNormalInput - MPlane1.MPlaneConcrete.MX, MPlane1.MPlaneConcrete.MNormal);
	// Used to be Distance2 = MPlane2.PhiWithNormal(x, Normal2); but that would trigger 
	const float Distance2 = MHeight - Distance1;          // the ensure on Distance2 being slightly larger than MHeight in some border cases
	const FVector SideVector = (PhiWithNormalInput - (MPlane1.MPlaneConcrete.MNormal * Distance1 + MPlane1.MPlaneConcrete.MX));
	const float Alpha = Distance1 / MHeight;
	const float Radius = MRadius1 * (1.0f - Alpha) + MRadius2 * Alpha;
	const float SideDistance = VectorSize(SideVector) - Radius;
	const float TopDistance = Distance1 < Distance2 ? Distance1 : Distance2;

	if (Distance1 < SMALL_NUMBER)
	{
		const FVector v = PhiWithNormalInput - (MPlane1.MPlaneConcrete.MNormal * Distance1 + MPlane1.MPlaneConcrete.MX);
		if (VectorSize(v) > MRadius1)
		{
			const FVector Corner = VectorGetSafeNormal(v) * MRadius1 + MPlane1.MPlaneConcrete.MX;
			const FVector CornerVector = PhiWithNormalInput - Corner;
			Normal = VectorGetSafeNormal(CornerVector);
			Phi = VectorSize(CornerVector);
		}
		else
		{
			Normal = MPlane1.MPlaneConcrete.MNormal * -1.0f;
			Phi = -Distance1;
		}
	}
	else if (Distance2 < SMALL_NUMBER)
	{
		const FVector v = PhiWithNormalInput - (MPlane2.MPlaneConcrete.MNormal * Distance2 + MPlane2.MPlaneConcrete.MX);
		if (VectorSize(v) > MRadius2)
		{
			const FVector Corner = VectorGetSafeNormal(v) * MRadius2 + MPlane2.MPlaneConcrete.MX;
			const FVector CornerVector = PhiWithNormalInput - Corner;
			Normal = VectorGetSafeNormal(CornerVector);
			Phi = VectorSize(CornerVector);
		}
		else
		{
			Normal = MPlane2.MPlaneConcrete.MNormal * -1.0f;
			Phi = -Distance2;
		}
	}
	else if (SideDistance < 0.0f && TopDistance < -SideDistance)
	{
		Normal = Distance1 < Distance2 ? MPlane1.MPlaneConcrete.MNormal * -1.0f : MPlane2.MPlaneConcrete.MNormal * -1.0f;
		Phi = -TopDistance;
	}
	else
	{
		Normal = VectorGetSafeNormal(SideVector);
		Phi = SideDistance;
	}
}

static inline void PhiWithNormalUnion(const uniform uint8 *uniform TypedPtr, const FVector &PhiWithNormalInput, FVector &Normal, float &Phi, const uniform int SizeofFImplicitObject, const uniform int OffsetOfMargin)
{
	const uniform UniquePtr *uniform MObjects = (const uniform UniquePtr *uniform)TypedPtr;
	Phi = FLT_MAX;
	bool NeedsNormalize = false;
	
	uniform uint8 *uniform Object = MObjects->Data[0].Ptr;
	uniform uint8 *uniform TypedObjectPtr = Object + SizeofFImplicitObject;

	FVector NextNormal;
	float NextPhi;

	PhiWithNormalTaperedCylinder(TypedObjectPtr, PhiWithNormalInput, NextNormal, NextPhi, SizeofFImplicitObject);

	if (NextPhi < Phi)
	{
		Phi = NextPhi;
		Normal = NextNormal;
		NeedsNormalize = false;
	}
	else if (NextPhi == Phi)
	{
		Normal = Normal + NextNormal;
		NeedsNormalize = true;
	}

	Object = MObjects->Data[1].Ptr;
	uniform float Margin = *((const uniform float *uniform)(Object + OffsetOfMargin));
	TypedObjectPtr = Object + SizeofFImplicitObject;

	PhiWithNormalSphere(TypedObjectPtr, PhiWithNormalInput, NextNormal, NextPhi, Margin);

	if (NextPhi < Phi)
	{
		Phi = NextPhi;
		Normal = NextNormal;
		NeedsNormalize = false;
	}
	else if (NextPhi == Phi)
	{
		Normal = Normal + NextNormal;
		NeedsNormalize = true;
	}

	Object = MObjects->Data[2].Ptr;
	Margin = *((const uniform float *uniform)(Object + OffsetOfMargin));
	TypedObjectPtr = Object + SizeofFImplicitObject;

	PhiWithNormalSphere(TypedObjectPtr, PhiWithNormalInput, NextNormal, NextPhi, Margin);

	if (NextPhi < Phi)
	{
		Phi = NextPhi;
		Normal = NextNormal;
		NeedsNormalize = false;
	}
	else if (NextPhi == Phi)
	{
		Normal = Normal + NextNormal;
		NeedsNormalize = true;
	}

	if(NeedsNormalize)
	{
		VectorNormalize(Normal);
	}
}

extern "C" unmasked void GetPhiWithNormal(const uniform uint8 *uniform CollisionParticles, const uniform float *uniform V, uniform float *uniform Normal, uniform float *uniform Phi, const uniform int i, const uniform int ProgramCount, const uniform int Mask);

export void ApplyPerParticleCollision(uniform FVector ParticlesP[],
										const uniform FVector ParticlesX[],
										const uniform float ParticlesInvM[],
										const uniform FVector CollisionV[],
										const uniform FVector CollisionX[],
										const uniform FVector CollisionW[],
										const uniform FVector4 CollisionR[],
										const uniform uint32 DynamicGroupIds[],
										const uniform uint32 KinematicGroupIds[],
										const uniform float PerGroupFriction[],
										const uniform float PerGroupThickness[],
										const uniform uint8 *uniform CollisionParticles,
										const uniform uint8 *uniform Geometry,
										const uniform int SizeofFImplicitObject,
										const uniform int OffsetofGeometryType,
										const uniform int OffsetOfMargin,
										const uniform float Dt,
										const uniform int InnerOffset,
										const uniform int InnerRange,
										const uniform int OuterOffset,
										const uniform int OuterRange)
{
	uniform int BailAt = OuterRange;
	uniform bool bBail = false;

	// Pre-check for bail out.
	foreach(Index = OuterOffset ... BailAt)
	{
		const uint32 DynamicGroupId = DynamicGroupIds[Index]; // Particle group Id

		for(uniform int i = InnerOffset; i < InnerRange; i++)
		{
			const uniform uint32 KinematicGroupId = KinematicGroupIds[i];  // Collision group Id

			if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
			{
				foreach_active(j)
				{
					if(!bBail)
					{
						BailAt = extract(Index, j);
						bBail = true;
					}
				}
			}
		}
	}

	foreach(Index = OuterOffset ... BailAt)
	{
		const float IM = ParticlesInvM[Index];

		if(all(IM == 0))
		{
			continue;
		}

		FVector P = VectorLoad(&ParticlesP[extract(Index, 0)]);

		const uint32 DynamicGroupId = DynamicGroupIds[Index]; // Particle group Id

		for(uniform int i = InnerOffset; i < InnerRange; i++) // SequentialFor
		{
			const uniform FTransform Frame = SetTransform(CollisionR[i], SetVector4(CollisionX[i], 0.0f), Float1110);
			const FVector PhiWithNormalInput = InverseTransformPosition(Frame, P);
			FVector Normal;
			float Phi;

			const uniform SerializedPtr *uniform GeometryPtr = (const uniform SerializedPtr *uniform)Geometry;
			const uniform uint8 *uniform Ptr = GeometryPtr[i].Ptr;
			const uniform uint8 *uniform TypedPtr = Ptr + SizeofFImplicitObject;
			const uniform uint8 Type = Ptr[OffsetofGeometryType] & 0x3F;

			switch(Type)
			{
			case Sphere:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalSphere(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Capsule:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Union:
				{
					PhiWithNormalUnion(TypedPtr, PhiWithNormalInput, Normal, Phi, SizeofFImplicitObject, OffsetOfMargin);
					break;
				}
			case TaperedCapsule:
				{
					PhiWithNormalTaperedCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi);
					break;
				}
			default:
				{
					uniform int IMMask;
					if(IM != 0)
					{
						IMMask = lanemask();
					}

					GetPhiWithNormal(CollisionParticles, (const uniform float *uniform)&PhiWithNormalInput, (uniform float *uniform)&Normal, (uniform float *uniform)&Phi, i, programCount, IMMask);
					break;
				}
			}

			#pragma ignore warning(perf)
			const float Thickness = PerGroupThickness[DynamicGroupId];

			if(Phi < Thickness)
			{
				const FVector NormalWorld = TransformVector(Frame, Normal);
				P = P + ((-Phi + Thickness) * NormalWorld);

				#pragma ignore warning(perf)
				const float CoefficientOfFriction = PerGroupFriction[DynamicGroupId];

				if(CoefficientOfFriction > 0.0f)
				{
					const uniform float MaximumFrictionCorrectionPerStep = 1.0f; // 1cm absolute maximum correction the friction force can provide
					const float Penetration = (-Phi + Thickness); // This is related to the Normal impulse

					FVector VectorToPoint = P - CollisionX[i];
					const FVector X = VectorLoad(&ParticlesX[extract(Index, 0)]);
					const FVector RelativeDisplacement = (P - X) - (CollisionV[i] + VectorCross(CollisionW[i], VectorToPoint)) * Dt; // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
					const FVector RelativeDisplacementTangent = RelativeDisplacement - VectorDot(RelativeDisplacement, NormalWorld) * NormalWorld; // Project displacement into the tangential plane
					const float RelativeDisplacementTangentLength = VectorSize(RelativeDisplacementTangent);
					float PositionCorrection = clamp(Penetration * CoefficientOfFriction, 0.0f, RelativeDisplacementTangentLength);
					if (PositionCorrection > MaximumFrictionCorrectionPerStep)
					{
						PositionCorrection = MaximumFrictionCorrectionPerStep;
					}
					if (PositionCorrection > 0)
					{
						P = P - ((PositionCorrection / RelativeDisplacementTangentLength) * RelativeDisplacementTangent);
					}
				}
			}
		}

		if(IM != 0)
		{
			VectorStore(&ParticlesP[extract(Index, 0)], P);
		}
	}
}

export void ApplyPerParticleCollisionFastFriction(uniform FVector ParticlesP[],
													const uniform FVector ParticlesX[],
													const uniform float ParticlesInvM[],
													const uniform FVector CollisionV[],
													const uniform FVector CollisionX[],
													const uniform FVector CollisionW[],
													const uniform FVector4 CollisionR[],
													const uniform uint32 DynamicGroupId,
													const uniform uint32 KinematicGroupIds[],
													const uniform float PerGroupFriction,
													const uniform float PerGroupThickness,
													const uniform uint8 *uniform CollisionParticles,
													const uniform uint8 *uniform Geometry,
													const uniform int SizeofFImplicitObject,
													const uniform int OffsetofGeometryType,
													const uniform int OffsetOfMargin,
													const uniform float Dt,
													const uniform int InnerOffset,
													const uniform int InnerRange,
													const uniform int OuterOffset,
													const uniform int OuterRange)
{
	uniform int BailAt = OuterRange;
	uniform bool bBail = false;

	// Pre-check for bail out.
	foreach(Index = OuterOffset ... BailAt)
	{
		for(uniform int i = InnerOffset; i < InnerRange; i++)
		{
			const uniform uint32 KinematicGroupId = KinematicGroupIds[i];  // Collision group Id

			if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
			{
				foreach_active(j)
				{
					if(!bBail)
					{
						BailAt = extract(Index, j);
						bBail = true;
					}
				}
			}
		}
	}

	foreach(Index = OuterOffset ... BailAt)
	{
		const float IM = ParticlesInvM[Index];

		if(all(IM == 0))
		{
			continue;
		}

		FVector P = VectorLoad(&ParticlesP[extract(Index, 0)]);

		for(uniform int i = InnerOffset; i < InnerRange; i++) // SequentialFor
		{
			const uniform FTransform Frame = SetTransform(CollisionR[i], SetVector4(CollisionX[i], 0.0f), Float1110);
			const FVector PhiWithNormalInput = InverseTransformPosition(Frame, P);
			FVector Normal;
			float Phi;

			const uniform SerializedPtr *uniform GeometryPtr = (const uniform SerializedPtr *uniform)Geometry;
			const uniform uint8 *uniform Ptr = GeometryPtr[i].Ptr;
			const uniform uint8 *uniform TypedPtr = Ptr + SizeofFImplicitObject;
			const uniform uint8 Type = Ptr[OffsetofGeometryType] & 0x3F;

			switch(Type)
			{
			case Sphere:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalSphere(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Capsule:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Union:
				{
					PhiWithNormalUnion(TypedPtr, PhiWithNormalInput, Normal, Phi, SizeofFImplicitObject, OffsetOfMargin);
					break;
				}
			case TaperedCapsule:
				{
					PhiWithNormalTaperedCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi);
					break;
				}
			default:
				{
					uniform int IMMask;
					if(IM != 0)
					{
						IMMask = lanemask();
					}

					GetPhiWithNormal(CollisionParticles, (const uniform float *uniform)&PhiWithNormalInput, (uniform float *uniform)&Normal, (uniform float *uniform)&Phi, i, programCount, IMMask);
					break;
				}
			}

			const float Penetration = PerGroupThickness - Phi; // This is related to the Normal impulse
			if(Penetration > 0.0f)
			{
				const FVector NormalWorld = TransformVector(Frame, Normal);
				P = P + (Penetration * NormalWorld);

				const FVector VectorToPoint = P - CollisionX[i];
				const FVector X = VectorLoad(&ParticlesX[extract(Index, 0)]);
				const FVector RelativeDisplacement = (P - X) - (CollisionV[i] + VectorCross(CollisionW[i], VectorToPoint)) * Dt; // This corresponds to the tangential velocity multiplied by dt (friction will drive this to zero if it is high enough)
				const FVector RelativeDisplacementTangent = RelativeDisplacement - VectorDot(RelativeDisplacement, NormalWorld) * NormalWorld; // Project displacement into the tangential plane
				const float RelativeDisplacementTangentLength = VectorSize(RelativeDisplacementTangent);

				const float PositionCorrection = min(Penetration * PerGroupFriction, RelativeDisplacementTangentLength);
				const float CorrectionRatio = select(RelativeDisplacementTangentLength < SMALL_NUMBER, 0.0f, PositionCorrection / RelativeDisplacementTangentLength);
				P = P - (CorrectionRatio * RelativeDisplacementTangent);
			}
		}

		if(IM != 0)
		{
			VectorStore(&ParticlesP[extract(Index, 0)], P);
		}
	}
}

export void ApplyPerParticleCollisionNoFriction(uniform FVector ParticlesP[],
												const uniform FVector ParticlesX[],
												const uniform float ParticlesInvM[],
												const uniform FVector CollisionV[],
												const uniform FVector CollisionX[],
												const uniform FVector CollisionW[],
												const uniform FVector4 CollisionR[],
												const uniform uint32 DynamicGroupId,
												const uniform uint32 KinematicGroupIds[],
												const uniform float PerGroupThickness,
												const uniform uint8 *uniform CollisionParticles,
												const uniform uint8 *uniform Geometry,
												const uniform int SizeofFImplicitObject,
												const uniform int OffsetofGeometryType,
												const uniform int OffsetOfMargin,
												const uniform float Dt,
												const uniform int InnerOffset,
												const uniform int InnerRange,
												const uniform int OuterOffset,
												const uniform int OuterRange)
{
	uniform int BailAt = OuterRange;
	uniform bool bBail = false;

	// Pre-check for bail out.
	foreach(Index = OuterOffset ... BailAt)
	{
		for(uniform int i = InnerOffset; i < InnerRange; i++)
		{
			const uniform uint32 KinematicGroupId = KinematicGroupIds[i];  // Collision group Id

			if (KinematicGroupId != (uint32)INDEX_NONE && DynamicGroupId != KinematicGroupId)
			{
				foreach_active(j)
				{
					if(!bBail)
					{
						BailAt = extract(Index, j);
						bBail = true;
					}
				}
			}
		}
	}

	foreach(Index = OuterOffset ... BailAt)
	{
		const float IM = ParticlesInvM[Index];

		if(all(IM == 0))
		{
			continue;
		}

		FVector P = VectorLoad(&ParticlesP[extract(Index, 0)]);

		for(uniform int i = InnerOffset; i < InnerRange; i++) // SequentialFor
		{
			const uniform FTransform Frame = SetTransform(CollisionR[i], SetVector4(CollisionX[i], 0.0f), Float1110);
			const FVector PhiWithNormalInput = InverseTransformPosition(Frame, P);
			FVector Normal;
			float Phi;

			const uniform SerializedPtr *uniform GeometryPtr = (const uniform SerializedPtr *uniform)Geometry;
			const uniform uint8 *uniform Ptr = GeometryPtr[i].Ptr;
			const uniform uint8 *uniform TypedPtr = Ptr + SizeofFImplicitObject;
			const uniform uint8 Type = Ptr[OffsetofGeometryType] & 0x3F;

			switch(Type)
			{
			case Sphere:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalSphere(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Capsule:
				{
					const uniform float Margin = *((const uniform float *uniform)(Ptr + OffsetOfMargin));
					PhiWithNormalCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi, Margin);
					break;
				}
			case Union:
				{
					PhiWithNormalUnion(TypedPtr, PhiWithNormalInput, Normal, Phi, SizeofFImplicitObject, OffsetOfMargin);
					break;
				}
			case TaperedCapsule:
				{
					PhiWithNormalTaperedCapsule(TypedPtr, PhiWithNormalInput, Normal, Phi);
					break;
				}
			default:
				{
					uniform int IMMask;
					if(IM != 0)
					{
						IMMask = lanemask();
					}

					GetPhiWithNormal(CollisionParticles, (const uniform float *uniform)&PhiWithNormalInput, (uniform float *uniform)&Normal, (uniform float *uniform)&Phi, i, programCount, IMMask);
					break;
				}
			}

			const float Penetration = PerGroupThickness - Phi; // This is related to the Normal impulse
			if(Penetration > 0.0f)
			{
				const FVector NormalWorld = TransformVector(Frame, Normal);
				P = P + (Penetration * NormalWorld);
			}
		}

		if(IM != 0)
		{
			VectorStore(&ParticlesP[extract(Index, 0)], P);
		}
	}
}
