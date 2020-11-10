// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Chaos/Matrix33.isph"
#include "Math/Quat.isph"
#include "Math/Vector.isph"

#define SMALL_NUMBER		(1.e-8f)
#define KINDA_SMALL_NUMBER	(1.e-4f)

struct FTransform
{
	FVector4 Rotation;
	FVector4 Translation;
	FVector4 Scale3D;
};

struct TArrayND
{
	uniform FIntVector MCounts;
	uniform float * uniform MArray;
};

/**
 * Calculates the cross product of two vectors (XYZ components). W is set to 0.
 *
 * @param Vec1	1st vector
 * @param Vec2	2nd vector
 * @return		cross(Vec1.xyz, Vec2.xyz). W is set to 0.
 */
inline static FVector4 VectorCross(const uniform FVector4& Vec1, const FVector4& Vec2)
{
	const uniform FVector4 A_YZXW = SetVector4(Vec1.V[1], Vec1.V[2], Vec1.V[0], Vec1.V[3]);
	const FVector4 B_ZXYW = SetVector4(Vec2.V[2], Vec2.V[0], Vec2.V[1], Vec2.V[3]);
	const uniform FVector4 A_ZXYW = SetVector4(Vec1.V[2], Vec1.V[0], Vec1.V[1], Vec1.V[3]);
	const FVector4 B_YZXW = SetVector4(Vec2.V[1], Vec2.V[2], Vec2.V[0], Vec2.V[3]);
	return (B_ZXYW * A_YZXW) - (B_YZXW * A_ZXYW);
}

/**
 * Rotate a vector using a unit Quaternion.
 *
 * @param Quat Unit Quaternion to use for rotation.
 * @param VectorW0 Vector to rotate. W component must be zero.
 * @return Vector after rotation by Quat.
 */
inline static FVector4 VectorQuaternionRotateVector(const uniform FVector4& Quat, const FVector4& VectorW0)
{
	// Q * V * Q.Inverse
	//const VectorRegister InverseRotation = VectorQuaternionInverse(Quat);
	//const VectorRegister Temp = VectorQuaternionMultiply2(Quat, VectorW0);
	//const VectorRegister Rotated = VectorQuaternionMultiply2(Temp, InverseRotation);

	// Equivalence of above can be shown to be:
	// http://people.csail.mit.edu/bkph/articles/Quaternions.pdf
	// V' = V + 2w(Q x V) + (2Q x (Q x V))
	// refactor:
	// V' = V + w(2(Q x V)) + (Q x (2(Q x V)))
	// T = 2(Q x V);
	// V' = V + w*(T) + (Q x T)

	const FVector4 QW = SetVector4(Quat.V[3], Quat.V[3], Quat.V[3], Quat.V[3]);
	FVector4 T = VectorCross(Quat, VectorW0);
	T = T + T;
	const FVector4 VTemp0 = (QW * T) + VectorW0;
	const FVector4 VTemp1 = VectorCross(Quat, T);
	const FVector4 Rotated = VTemp0 + VTemp1;
	return Rotated;
}

inline static FVector TransformPositionNoScale(const uniform FTransform &T, const FVector& V)
{
	FVector4 InputVectorW0 = SetVector4(V.V[0], V.V[1], V.V[2], 0.0f);

	//Transform using QST is following
	//QST(P) = Q.Rotate(S*P) + T where Q = quaternion, S = 1.0f, T = translation

	//RotatedVec = Q.Rotate(V.X, V.Y, V.Z, 0.f)
	const FVector4 RotatedVec = VectorQuaternionRotateVector(T.Rotation, InputVectorW0);
	const FVector4 TranslatedVec = RotatedVec + T.Translation;

	return SetVector(TranslatedVec.V[0], TranslatedVec.V[1], TranslatedVec.V[2]);
}

inline static float SquaredSum(const FVector &Vec)
{
	float SquareSum = 0.0f;
	for(uniform int i = 0; i < 3; i++)
	{
		SquareSum += (Vec.V[i] * Vec.V[i]);
	}
	return SquareSum;
}

inline static float VectorSize2(const FVector &Normal)
{
	return rcp(rsqrt(SquaredSum(Normal)));
}

inline static float BoxPhi(const uniform FVector &BoxMin, const uniform FVector &BoxMax, const FVector& LocalPoint)
{
	const FVector MaxDists = LocalPoint - BoxMax;
	const FVector MinDists = BoxMin - LocalPoint;
	float Phi;
	if (IsVectorLessEqual(LocalPoint, BoxMax) && IsVectorGreaterEqual(LocalPoint, BoxMin))
	{
		const FVector LocalMax = VectorMax(MinDists, MaxDists);
		if (LocalMax.V[0] > LocalMax.V[1])
		{
			if (LocalMax.V[0] > LocalMax.V[2])
			{
				Phi = LocalMax.V[0];
			}
			else
			{
				Phi = LocalMax.V[2];
			}
		}
		else
		{
			if (LocalMax.V[1] > LocalMax.V[2])
			{
				Phi = LocalMax.V[1];
			}
			else
			{
				Phi = LocalMax.V[2];
			}
		}
	}
	else
	{
		FVector Normal;
		for(uniform int i = 0; i < 3; i++)
		{
			if (MaxDists.V[i] > 0)
			{
				Normal.V[i] = MaxDists.V[i];
			}
			else if (MinDists.V[i] > 0)
			{
				Normal.V[i] = -MinDists.V[i];
			}
			else
			{
				Normal.V[i] = 0;
			}
		}
		Phi = VectorSize2(Normal);
	}

	return Phi;
}

export void SampleBoxNormalAverage(const uniform FVector &BoxMin,
									const uniform FVector &BoxMax, 
									const uniform FTransform &SampleToObjectTransform, 
									const uniform FVector SampleParticles[],
									const uniform int PotentialParticles[],
									const uniform float Thickness, 
									uniform float &TotalThickness, 
									uniform FVector &ConstraintLocation,
									const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorGather(&SampleParticles[PotentialParticles[i]]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SampleBoxNoNormal(const uniform FVector &BoxMin,
									const uniform FVector &BoxMax, 
									const uniform FTransform &SampleToObjectTransform, 
									const uniform FVector SampleParticles[],
									const uniform int PotentialParticles[],
									uniform int &DeepestParticle,
									uniform float &Phi,
									const uniform unsigned int NumParticles)
{
	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		const int ParticleNum = PotentialParticles[i];

		FVector Particle = VectorGather(&SampleParticles[ParticleNum]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(ParticleNum, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}


export void SampleBoxNormalAverageAll(const uniform FVector &BoxMin,
									const uniform FVector &BoxMax, 
									const uniform FTransform &SampleToObjectTransform, 
									const uniform FVector SampleParticles[],
									const uniform float Thickness, 
									uniform float &TotalThickness, 
									uniform FVector &ConstraintLocation,
									const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SampleBoxNoNormalAll(const uniform FVector &BoxMin,
										const uniform FVector &BoxMax,
										const uniform FTransform &SampleToObjectTransform,
										const uniform FVector SampleParticles[],
										uniform int &DeepestParticle,
										uniform float &Phi,
										const uniform unsigned int NumParticles)
{
	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(i, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}

inline static FVector ClampMinusHalf(const uniform FVector &Min, const uniform FVector &Max, const FVector& LocalPoint)
{
	FVector Result;
	for (uniform int i = 0; i < 3; ++i)
	{
		if (LocalPoint.V[i] > Max.V[i])
		{
			Result.V[i] = Max.V[i];
		}
		else if (LocalPoint.V[i] < Min.V[i])
		{
			Result.V[i] = Min.V[i];
		}
		else
		{
			Result.V[i] = LocalPoint.V[i];
		}
	}
	return Result;
}

inline static float GetCell(const uniform TArrayND* uniform LevelSetPhiArray, const FIntVector &Vec)
{
	const int Offset = (Vec.V[0] * LevelSetPhiArray->MCounts.V[1] + Vec.V[1]) * LevelSetPhiArray->MCounts.V[2] + Vec.V[2];

	#pragma ignore warning(perf)
	return LevelSetPhiArray->MArray[Offset];
}

inline static float LinearlyInterpolate1D(const float& Prev, const float& Next, const float Alpha)
{
	return Prev + (Next - Prev) * Alpha;
}

inline static FVector Location(const uniform FVector &MinCorner, const uniform FVector &Dx, const FIntVector& X)
{
	return Dx * X + MinCorner + (Dx / 2);
}

inline static float LinearlyInterpolate(const uniform FVector &MinCorner,
							const uniform FVector &MaxCorner,
							const uniform FVector &Dx,
							const uniform FIntVector &Cells,
							const uniform TArrayND* uniform LevelSetPhiArray, 
							const FVector& X)
{
	FIntVector XCell = SetIntVector((X - MinCorner) / Dx);
	
	FVector XCenter = Location(MinCorner, Dx, XCell);
	FIntVector CellPrev;
	for (uniform int i = 0; i < 3; i++)
	{
		CellPrev.V[i] = X.V[i] > XCenter.V[i] ? XCell.V[i] : XCell.V[i] - 1;
	}
	FVector Alpha = (X - Location(MinCorner, Dx, CellPrev)) / Dx;
	
	// Clamp correctly when on border
	for (uniform int i = 0; i < 3; i++)
	{
		if (CellPrev.V[i] == -1)
		{
			CellPrev.V[i] = 0;
			Alpha.V[i] = 0;
		}
		if (CellPrev.V[i] == Cells.V[i] - 1)
		{
			CellPrev.V[i] = Cells.V[i] - 2;
			Alpha.V[i] = 1;
		}
	}

	const float interpx1 = LinearlyInterpolate1D(GetCell(LevelSetPhiArray, CellPrev), GetCell(LevelSetPhiArray, CellPrev + SetIntVector(1, 0, 0)), Alpha.V[0]);
	const float interpx2 = LinearlyInterpolate1D(GetCell(LevelSetPhiArray, CellPrev + SetIntVector(0, 1, 0)), GetCell(LevelSetPhiArray, CellPrev + SetIntVector(1, 1, 0)), Alpha.V[0]);
	const float interpx3 = LinearlyInterpolate1D(GetCell(LevelSetPhiArray, CellPrev + SetIntVector(0, 0, 1)), GetCell(LevelSetPhiArray, CellPrev + SetIntVector(1, 0, 1)), Alpha.V[0]);
	const float interpx4 = LinearlyInterpolate1D(GetCell(LevelSetPhiArray, CellPrev + SetIntVector(0, 1, 1)), GetCell(LevelSetPhiArray, CellPrev + SetIntVector(1, 1, 1)), Alpha.V[0]);
	const float interpy1 = LinearlyInterpolate1D(interpx1, interpx2, Alpha.V[1]);
	const float interpy2 = LinearlyInterpolate1D(interpx3, interpx4, Alpha.V[1]);
	return LinearlyInterpolate1D(interpy1, interpy2, Alpha.V[2]);
}

export void SampleLevelSetNormalAverage(const uniform FVector &GridMinCorner,
										const uniform FVector &GridMaxCorner,
										const uniform FVector &GridDx,
										const uniform FIntVector &GridCounts,
										const uniform TArrayND* uniform LevelSetPhiArray,
										const uniform FTransform &SampleToObjectTransform, 
										const uniform FVector SampleParticles[],
										const uniform int PotentialParticles[],
										const uniform float Thickness, 
										uniform float &TotalThickness, 
										uniform FVector &ConstraintLocation,
										const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	uniform FVector GridClampMax = GridMaxCorner - GridDx * 0.5f;
	uniform FVector GridClampMin = GridMinCorner + GridDx * 0.5f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorGather(&SampleParticles[PotentialParticles[i]]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		FVector Location = ClampMinusHalf(GridClampMin, GridClampMax, LocalPoint);
		float SizeSquared = SquaredSum(Location - LocalPoint);
		float LocalPhi = LinearlyInterpolate(GridMinCorner, GridMaxCorner, GridDx, GridCounts, LevelSetPhiArray, Location);

		if(SizeSquared)
		{
			LocalPhi = rcp(rsqrt(SizeSquared)) + LocalPhi;
		}

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SampleLevelSetNoNormal(const uniform FVector &GridMinCorner,
										const uniform FVector &GridMaxCorner,
										const uniform FVector &GridDx,
										const uniform FIntVector &GridCounts,
										const uniform TArrayND* uniform LevelSetPhiArray,
										const uniform FTransform &SampleToObjectTransform, 
										const uniform FVector SampleParticles[],
										const uniform int PotentialParticles[],
										uniform int &DeepestParticle,
										uniform float &Phi,
										const uniform unsigned int NumParticles)
{
	uniform FVector GridClampMax = GridMaxCorner - GridDx * 0.5f;
	uniform FVector GridClampMin = GridMinCorner + GridDx * 0.5f;

	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		const int ParticleNum = PotentialParticles[i];

		FVector Particle = VectorGather(&SampleParticles[ParticleNum]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		FVector Location = ClampMinusHalf(GridClampMin, GridClampMax, LocalPoint);
		float SizeSquared = SquaredSum(Location - LocalPoint);
		float LocalPhi = LinearlyInterpolate(GridMinCorner, GridMaxCorner, GridDx, GridCounts, LevelSetPhiArray, Location);

		if(SizeSquared)
		{
			LocalPhi = rcp(rsqrt(SizeSquared)) + LocalPhi;
		}

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(ParticleNum, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}

export void SampleLevelSetNormalAverageAll(const uniform FVector &GridMinCorner,
										const uniform FVector &GridMaxCorner,
										const uniform FVector &GridDx,
										const uniform FIntVector &GridCounts,
										const uniform TArrayND* uniform LevelSetPhiArray,
										const uniform FTransform &SampleToObjectTransform, 
										const uniform FVector SampleParticles[],
										const uniform float Thickness, 
										uniform float &TotalThickness, 
										uniform FVector &ConstraintLocation,
										const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	uniform FVector GridClampMax = GridMaxCorner - GridDx * 0.5f;
	uniform FVector GridClampMin = GridMinCorner + GridDx * 0.5f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		FVector Location = ClampMinusHalf(GridClampMin, GridClampMax, LocalPoint);
		float SizeSquared = SquaredSum(Location - LocalPoint);
		float LocalPhi = LinearlyInterpolate(GridMinCorner, GridMaxCorner, GridDx, GridCounts, LevelSetPhiArray, Location);

		if(SizeSquared)
		{
			LocalPhi = rcp(rsqrt(SizeSquared)) + LocalPhi;
		}

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SampleLevelSetNoNormalAll(const uniform FVector &GridMinCorner,
											const uniform FVector &GridMaxCorner,
											const uniform FVector &GridDx,
											const uniform FIntVector &GridCounts,
											const uniform TArrayND* uniform LevelSetPhiArray,
											const uniform FTransform &SampleToObjectTransform, 
											const uniform FVector SampleParticles[],
											uniform int &DeepestParticle,
											uniform float &Phi,
											const uniform unsigned int NumParticles)
{
	uniform FVector GridClampMax = GridMaxCorner - GridDx * 0.5f;
	uniform FVector GridClampMin = GridMinCorner + GridDx * 0.5f;

	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		FVector Location = ClampMinusHalf(GridClampMin, GridClampMax, LocalPoint);
		float SizeSquared = SquaredSum(Location - LocalPoint);
		float LocalPhi = LinearlyInterpolate(GridMinCorner, GridMaxCorner, GridDx, GridCounts, LevelSetPhiArray, Location);

		if(SizeSquared)
		{
			LocalPhi = rcp(rsqrt(SizeSquared)) + LocalPhi;
		}

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(i, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}

inline static float DotProduct(const FVector &V1, const uniform FVector &V2)
{
	return V1.V[0] * V2.V[0] + V1.V[1] * V2.V[1] + V1.V[2] * V2.V[2];
}

export void SamplePlaneNormalAverageAll(const uniform FVector &ObjectNormal,
									const uniform FVector &ObjectMX,
									const uniform FTransform &SampleToObjectTransform, 
									const uniform FVector SampleParticles[],
									const uniform float Thickness, 
									uniform float &TotalThickness, 
									uniform FVector &ConstraintLocation,
									const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = DotProduct(LocalPoint - ObjectMX, ObjectNormal);

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SamplePlaneNoNormalAll(const uniform FVector &ObjectNormal,
										const uniform FVector &ObjectMX,
										const uniform FTransform &SampleToObjectTransform, 
										const uniform FVector SampleParticles[],
										uniform int &DeepestParticle,
										uniform float &Phi,
										const uniform unsigned int NumParticles)
{
	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = DotProduct(LocalPoint - ObjectMX, ObjectNormal);

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(i, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}

export void SampleSphereNormalAverageAll(const uniform float &ObjectRadius,
									const uniform FVector &ObjectMX,
									const uniform FTransform &SampleToObjectTransform, 
									const uniform FVector SampleParticles[],
									const uniform float Thickness, 
									uniform float &TotalThickness, 
									uniform FVector &ConstraintLocation,
									const uniform unsigned int NumParticles)
{
	FVector ConstraintLocationSum = SetVector(0.0f, 0.0f, 0.0f);
	float ThicknessSum = 0.0f;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = ObjectRadius - VectorSize2(LocalPoint - ObjectMX);

		float LocalThickness = LocalPhi - Thickness;
		if(LocalThickness < -KINDA_SMALL_NUMBER)
		{
			ConstraintLocationSum = ConstraintLocationSum + (LocalPoint * LocalThickness);
			ThicknessSum += LocalThickness;
		}
	}

	ConstraintLocation = ConstraintLocation + SetVector(reduce_add(ConstraintLocationSum.V[0]), reduce_add(ConstraintLocationSum.V[1]), reduce_add(ConstraintLocationSum.V[2]));
	TotalThickness = TotalThickness + reduce_add(ThicknessSum);
}

export uniform bool SampleSphereNoNormalAll(const uniform float &ObjectRadius,
										const uniform FVector &ObjectMX,
										const uniform FTransform &SampleToObjectTransform, 
										const uniform FVector SampleParticles[],
										uniform int &DeepestParticle,
										uniform float &Phi,
										const uniform unsigned int NumParticles)
{
	uniform bool bPhiFound = false;

	foreach(i = 0 ... NumParticles)
	{
		FVector Particle = VectorLoad(&SampleParticles[extract(i, 0)]);

		FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
		float LocalPhi = ObjectRadius - VectorSize2(LocalPoint - ObjectMX);

		if(LocalPhi < Phi)
		{
			Phi = reduce_min(LocalPhi);
			foreach_active(j)
			{
				if(extract(LocalPhi, j) == Phi)
				{
					DeepestParticle = extract(i, j);
				}
			}
			bPhiFound = true;
		}
	}

	return bPhiFound;
}


//
//
///////////////////////////////////////////////////////////////////////////////
//
//

struct FCollisionContact
{
	bool bDisabled;
	FVector Normal;
	FVector Location;
	float Phi;

	float Friction;
	float AngularFriction;
	float Restitution;
	float InvInertiaScale0;
	float InvInertiaScale1;
	int ShapesType;
	int* Implicit[2];
};

export uniform int32 SizeofFCollisionContact()
{
	return sizeof(uniform FCollisionContact);
}

// Get the CoM position given the Actor position
static inline uniform FVector GetCoMWorldPosition(
	const uniform FVector& PActorWorld,
	const uniform FVector4& QActorWorld,
	const uniform FVector& PCoMLocal)
{
	return PActorWorld + VectorQuaternionRotateVector(QActorWorld, PCoMLocal);
}

// Get the CoM rotation given the Actor rotation
static inline uniform FVector4 GetCoMWorldRotation(
	const uniform FVector4& QActorWorld,
	const uniform FVector4& QCoMLocal)
{
	return VectorQuaternionMultiply2(QActorWorld, QCoMLocal);
}

// Set the Actor position and rotation given the CoM position and rotation
static inline uniform FVector SetCoMWorldTransform(
	uniform FVector& PActorWorld,
	uniform FVector4& QActorWorld,
	const uniform FVector& PCoMWorld,
	const uniform FVector4& QCoMWorld,
	const uniform FVector& PCoMLocal,
	const uniform FVector4& QCoMLocal)
{
	uniform FVector4 QCoMLocalInverse = QuatInverse(QCoMLocal);
	QActorWorld = VectorQuaternionMultiply2(QCoMWorld, QCoMLocalInverse);
	PActorWorld = PCoMWorld - VectorQuaternionRotateVector(QActorWorld, PCoMLocal);
}

// @todo(ccaulfield): dedupe
static inline uniform FMatrix33 ComputeWorldSpaceInertia(const uniform FVector4 &Q, const uniform FVector &I)
{
	const uniform FMatrix33 QM = MakeQuatRotationTranslationMatrix(Q);
	const uniform FMatrix33 L = SetMatrix33(I.V[0], I.V[1], I.V[2]);
	return MultiplyAB(QM, MultiplyABt(L, QM));
}

// @todo(ccaulfield): dedupe
static inline uniform FMatrix33 ComputeJointFactorMatrix(const uniform FVector& V, const uniform FMatrix33& M, const uniform float Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	const uniform FVector V221 = SetVector(-V.V[2], V.V[2], -V.V[1]);
	const uniform FVector V222 = SetVector(-V.V[2], -V.V[2], -V.V[2]);
	const uniform FVector M544 = SetVector(M.M[5], M.M[4], M.M[4]);
	const uniform FVector V111 = SetVector(V.V[1], V.V[1], V.V[1]);
	const uniform FVector M988 = SetVector(M.M[9], M.M[8], M.M[8]);
	const uniform FVector V100 = SetVector(V.V[1], -V.V[0], V.V[0]);
	const uniform FVector M995 = SetVector(M.M[9], M.M[9], M.M[5]);
	const uniform FVector M10109 = SetVector(M.M[10], M.M[10], M.M[9]);
	const uniform FVector IM = SetVector(Im, 0, 0);

	const uniform FVector R1 = (V221 * (V222 * M544 + V111 * M988)) + (V100 * (V222 * M995 + V111 * M10109)) + IM;

	const uniform FVector V211 = SetVector(V.V[2], -V.V[1], -V.V[1]);
	const uniform FVector V221_2 = SetVector(V.V[2], V.V[2], -V.V[1]);
	const uniform FVector M000 = SetVector(M.M[0], M.M[0], M.M[0]);
	const uniform FVector V000 = SetVector(-V.V[0], -V.V[0], V.V[0]);
	const uniform FVector M884 = SetVector(M.M[8], M.M[8], M.M[4]);
	const uniform FVector V000_2 = SetVector(-V.V[0], V.V[0], V.V[0]);
	const uniform FVector M844 = SetVector(M.M[8], M.M[4], M.M[4]);
	const uniform FVector M1095 = SetVector(M.M[10], M.M[9], M.M[5]);
	const uniform FVector IM_2 = SetVector(Im, 0, Im);

	const uniform FVector R2 = (V211 * (V221_2 * M000 + V000 * M884)) + (V000_2 * (V221_2 * M844 + V000 * M1095)) + IM_2;

	return SetMatrix33(R1.V[0], R1.V[1], R1.V[2], R2.V[0], R2.V[1], R2.V[2]);
}

// Apply the contact force. Update the actors' positions and rotations.
export void ApplyContact2(
	const uniform FCollisionContact* uniform Contact,
	uniform FVector& PActor0,
	uniform FVector4& QActor0,
	uniform FVector& PActor1,
	uniform FVector4& QActor1,
	const uniform float InvM0,
	const uniform FVector& InvILocal0,
	const uniform FVector& PCoMLocal0,
	const uniform FVector4& QCoMLocal0,
	const uniform float InvM1,
	const uniform FVector& InvILocal1,
	const uniform FVector& PCoMLocal1,
	const uniform FVector4& QCoMLocal1)
{
	uniform FVector P0 = GetCoMWorldPosition(PActor0, QActor0, PCoMLocal0);
	uniform FVector P1 = GetCoMWorldPosition(PActor1, QActor1, PCoMLocal1);
	uniform FVector4 Q0 = GetCoMWorldRotation(QActor0, QCoMLocal0);
	uniform FVector4 Q1 = GetCoMWorldRotation(QActor1, QCoMLocal1);
	uniform FVector VectorToPoint0 = Contact->Location - P0;
	uniform FVector VectorToPoint1 = Contact->Location - P1;
	
	uniform FMatrix33 ContactInvI = Matrix33Zero;
	uniform FMatrix33 InvI0 = Matrix33Zero;
	uniform FMatrix33 InvI1 = Matrix33Zero;
	if (InvM0 > 0.0f)
	{
		InvI0 = Multiply(ComputeWorldSpaceInertia(Q0, InvILocal0), Contact->InvInertiaScale0);
		ContactInvI = AddAB(ContactInvI, ComputeJointFactorMatrix(VectorToPoint0, InvI0, InvM0));
	}
	if (InvM1 > 0.0f)
	{
		InvI1 = Multiply(ComputeWorldSpaceInertia(Q1, InvILocal1), Contact->InvInertiaScale1);
		ContactInvI = AddAB(ContactInvI, ComputeJointFactorMatrix(VectorToPoint1, InvI1, InvM1));
	}
	
	// Calculate the normal correction
	// @todo(chaos): support restitution in this mode (requires a small-impact cutoff threshold)
	uniform float Restitution = 0.0f;//Contact->Restitution;
	uniform FVector NormalError = Contact->Phi * Contact->Normal;
	uniform float NormalImpulseDenominator = VectorDot(Contact->Normal, Multiply(ContactInvI, Contact->Normal));
	uniform FVector NormalImpulseNumerator = -(1.0f + Restitution) * NormalError;
	uniform FVector NormalCorrection = NormalImpulseNumerator / NormalImpulseDenominator;
	
	// Calculate the lateral correction
	uniform FVector LateralCorrection = ZeroVector;
	//if (Contact->Friction > 0)
	//{
	//	// Get contact velocity
	//	uniform FVector X0 = FParticleUtilitiesXR::GetCoMWorldPosition(Particle0);
	//	uniform FVector X1 = FParticleUtilitiesXR::GetCoMWorldPosition(Particle1);
	//	FRotation3 R0 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle0);
	//	FRotation3 R1 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle1);
	//	uniform FVector V0 = FVec3::CalculateVelocity(X0, P0, IterationParameters.Dt);
	//	uniform FVector W0 = FRotation3::CalculateAngularVelocity(R0, Q0, IterationParameters.Dt);
	//	uniform FVector V1 = FVec3::CalculateVelocity(X1, P1, IterationParameters.Dt);
	//	uniform FVector W1 = FRotation3::CalculateAngularVelocity(R1, Q1, IterationParameters.Dt);
	//	uniform FVector CV0 = V0 + FVec3::CrossProduct(W0, VectorToPoint0);
	//	uniform FVector CV1 = V1 + FVec3::CrossProduct(W1, VectorToPoint1);
	//	uniform FVector CV = CV0 - CV1;
	//
	//	// Calculate lateral correction, clamped to the friction cone. Kinda.
	//	uniform float CVNormalMag = FVec3::DotProduct(CV, Contact->Normal);
	//	if (CVNormalMag < 0.0f)
	//	{
	//		uniform FVector CVLateral = CV - CVNormalMag * Contact->Normal;
	//		uniform float CVLateralMag = CVLateral.Size();
	//		if (CVLateralMag > KINDA_SMALL_NUMBER)
	//		{
	//			uniform FVector DirLateral = CVLateral / CVLateralMag;
	//			uniform FVector LateralImpulseNumerator = -CVLateral * IterationParameters.Dt;
	//			uniform float LateralImpulseDenominator = FVec3::DotProduct(DirLateral, ContactInvI * DirLateral);
	//			LateralCorrection = LateralImpulseNumerator / LateralImpulseDenominator;
	//			uniform float LateralImpulseMag = LateralCorrection.Size();
	//			uniform float NormalImpulseMag = NormalCorrection.Size();
	//			if (LateralImpulseMag > Contact->Friction * NormalImpulseMag)
	//			{
	//				LateralCorrection *= Contact->Friction * NormalImpulseMag / LateralImpulseMag;
	//			}
	//		}
	//	}
	//}

	// Net Correction
	uniform FVector DX = NormalCorrection + LateralCorrection;
				
	if (InvM0 > 0.0f)
	{
		uniform FVector DP0 = InvM0 * DX;
		uniform FVector DR0 = Multiply(InvI0, VectorCross(VectorToPoint0, DX));
		uniform FVector4 DQ0 = VectorQuaternionMultiply2(SetVector4(DR0, 0.0f), Q0) * 0.5f;
	
		P0 = P0 + DP0;
		Q0 = VectorNormalizeQuaternion(Q0 + DQ0);
		
		SetCoMWorldTransform(PActor0, QActor0, P0, Q0, PCoMLocal0, QCoMLocal0);
	}

	if (InvM1 > 0.0f)
	{
		uniform FVector DXNeg = VectorNegate(DX);
		uniform FVector DP1 = InvM1 * DXNeg;
		uniform FVector DR1 = Multiply(InvI1, VectorCross(VectorToPoint1, DXNeg));
		uniform FVector4 DQ1 = VectorQuaternionMultiply2(SetVector4(DR1, 0.0f), Q1) * 0.5f;
		
		P1 = P1 + DP1;
		Q1 = VectorNormalizeQuaternion(Q1 + DQ1);
		
		SetCoMWorldTransform(PActor1, QActor1, P1, Q1, PCoMLocal1, QCoMLocal1);
	}
}
