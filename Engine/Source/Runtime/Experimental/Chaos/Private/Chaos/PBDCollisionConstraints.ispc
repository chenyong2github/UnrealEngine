// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

inline static float NormalSize(const FVector &Normal)
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
		Phi = NormalSize(Normal);
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
		#pragma ignore warning(perf)
		FVector Particle = SampleParticles[PotentialParticles[i]];

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
		if(!bPhiFound)
		{
			const int ParticleNum = PotentialParticles[i];

			#pragma ignore warning(perf)
			FVector Particle = SampleParticles[ParticleNum];

			FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
			float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

			if(LocalPhi < Phi)
			{
				foreach_active(j)
				{
					unmasked
					{
						if(!bPhiFound)
						{
							Phi = extract(LocalPhi, j);
							DeepestParticle = extract(ParticleNum, j);
							bPhiFound = true;
						}
					}
				}
			}
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
		#pragma ignore warning(perf)
		FVector Particle = SampleParticles[i];

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
		if(!bPhiFound)
		{
			#pragma ignore warning(perf)
			FVector Particle = SampleParticles[i];

			FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
			float LocalPhi = BoxPhi(BoxMin, BoxMax, LocalPoint);

			if(LocalPhi < Phi)
			{
				foreach_active(j)
				{
					unmasked
					{
						if(!bPhiFound)
						{
							Phi = extract(LocalPhi, j);
							DeepestParticle = extract(i, j);
							bPhiFound = true;
						}
					}
				}
			}
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
		#pragma ignore warning(perf)
		FVector Particle = SampleParticles[PotentialParticles[i]];

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
		if(!bPhiFound)
		{
			const int ParticleNum = PotentialParticles[i];

			#pragma ignore warning(perf)
			FVector Particle = SampleParticles[ParticleNum];

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
				foreach_active(j)
				{
					unmasked
					{
						if(!bPhiFound)
						{
							Phi = extract(LocalPhi, j);
							DeepestParticle = extract(ParticleNum, j);
							bPhiFound = true;
						}
					}
				}
			}
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
		#pragma ignore warning(perf)
		FVector Particle = SampleParticles[i];

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
		if(!bPhiFound)
		{
			#pragma ignore warning(perf)
			FVector Particle = SampleParticles[i];

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
				foreach_active(j)
				{
					unmasked
					{
						if(!bPhiFound)
						{
							Phi = extract(LocalPhi, j);
							DeepestParticle = extract(i, j);
							bPhiFound = true;
						}
					}
				}
			}
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
		#pragma ignore warning(perf)
		FVector Particle = SampleParticles[i];

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
		if(!bPhiFound)
		{
			#pragma ignore warning(perf)
			FVector Particle = SampleParticles[i];

			FVector LocalPoint = TransformPositionNoScale(SampleToObjectTransform, Particle);
			float LocalPhi = DotProduct(LocalPoint - ObjectMX, ObjectNormal);

			if(LocalPhi < Phi)
			{
				foreach_active(j)
				{
					unmasked
					{
						if(!bPhiFound)
						{
							Phi = extract(LocalPhi, j);
							DeepestParticle = extract(i, j);
							bPhiFound = true;
						}
					}
				}
			}
		}
	}

	return bPhiFound;
}