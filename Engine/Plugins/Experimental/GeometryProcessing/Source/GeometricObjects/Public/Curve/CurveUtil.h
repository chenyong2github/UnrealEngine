// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "SegmentTypes.h"
#include "LineTypes.h"
#include "RayTypes.h"
#include "BoxTypes.h"
#include "SegmentTypes.h"

/**
 * Curve utility functions
 */
template<typename RealType>
struct TCurveUtil
{

	static FVector3<RealType> Tangent(const TArrayView<const FVector3<RealType>>& Vertices, int32 Idx, bool bLoop = false)
	{
		int32 EndIdx = Idx + 1;
		int32 StartIdx = Idx - 1;
		int32 NV = Vertices.Num();
		if (bLoop)
		{
			EndIdx = EndIdx % NV;
			StartIdx = (StartIdx + NV) % NV;
		}
		else
		{
			EndIdx = FMath::Min(NV - 1, EndIdx);
			StartIdx = FMath::Max(0, StartIdx);
		}
		return (Vertices[EndIdx] - Vertices[StartIdx]).Normalized();
	}


	static RealType ArcLength(const TArrayView<const FVector3<RealType>>& Vertices, bool bLoop = false) {
		RealType Sum = 0;
		int32 NV = Vertices.Num();
		for (int i = 1; i < NV; ++i)
		{
			Sum += Vertices[i].Distance(Vertices[i - 1]);
		}
		if (bLoop)
		{
			Sum += Vertices[NV - 1].Distance(Vertices[0]);
		}
		return Sum;
	}

	static int FindNearestIndex(const TArrayView<const FVector3<RealType>>& Vertices, FVector3<RealType> V)
	{
		int iNearest = -1;
		double dNear = TMathUtil<double>::MaxReal;
		int N = Vertices.Num();
		for ( int i = 0; i < N; ++i )
		{
			double dSqr = Vertices[i].DistanceSquared(V);
			if (dSqr < dNear)
			{
				dNear = dSqr;
				iNearest = i;
			}
		}
		return iNearest;
	}


	/**
	 * smooth vertices in-place (will not produce a symmetric result, but does not require extra buffer)
	 */
	static void InPlaceSmooth(TArrayView<FVector3<RealType>> Vertices, int StartIdx, int EndIdx, double Alpha, int NumIterations, bool bClosed)
	{
		int N = Vertices.Num();
		if ( bClosed )
		{
			for (int Iter = 0; Iter < NumIterations; ++Iter)
			{
				for (int ii = StartIdx; ii < EndIdx; ++ii)
				{
					int i = (ii % N);
					int iPrev = (ii == 0) ? N - 1 : ii - 1;
					int iNext = (ii + 1) % N;
					FVector3<RealType> prev = Vertices[iPrev], next = Vertices[iNext];
					FVector3<RealType> c = (prev + next) * 0.5f;
					Vertices[i] = (1 - Alpha) * Vertices[i] + (Alpha) * c;
				}
			}
		}
		else
		{
			for (int Iter = 0; Iter < NumIterations; ++Iter)
			{
				for (int i = StartIdx; i < EndIdx; ++i)
				{
					if (i == 0 || i >= N - 1)
					{
						continue;
					}
					FVector3<RealType> prev = Vertices[i - 1], next = Vertices[i + 1];
					FVector3<RealType> c = (prev + next) * 0.5f;
					Vertices[i] = (1 - Alpha) * Vertices[i] + (Alpha) * c;
				}
			}
		}
	}



	/**
	 * smooth set of vertices using extra buffer
	 */
	static void IterativeSmooth(TArrayView<FVector3<RealType>> Vertices, int StartIdx, int EndIdx, double Alpha, int NumIterations, bool bClosed)
	{
		int N = Vertices.Num();
		TArray<FVector3<RealType>> Buffer;
		Buffer.SetNumZeroed(N);

		if (bClosed)
		{
			for (int Iter = 0; Iter < NumIterations; ++Iter)
			{
				for (int ii = StartIdx; ii < EndIdx; ++ii)
				{
					int i = (ii % N);
					int iPrev = (ii == 0) ? N - 1 : ii - 1;
					int iNext = (ii + 1) % N;
					FVector3<RealType> prev = Vertices[iPrev], next = Vertices[iNext];
					FVector3<RealType> c = (prev + next) * 0.5f;
					Buffer[i] = (1 - Alpha) * Vertices[i] + (Alpha) * c;
				}
				for (int ii = StartIdx; ii < EndIdx; ++ii)
				{
					int i = (ii % N);
					Vertices[i] = Buffer[i];
				}
			}
		}
		else
		{
			for (int Iter = 0; Iter < NumIterations; ++Iter)
			{
				for (int i = StartIdx; i < EndIdx && i < N; ++i)
				{
					if (i == 0 || i == N - 1)
					{
						continue;
					}
					FVector3<RealType> prev = Vertices[i - 1], next = Vertices[i + 1];
					FVector3<RealType> c = (prev + next) * 0.5f;
					Buffer[i] = (1 - Alpha) * Vertices[i] + (Alpha) * c;
				}
				for (int i = StartIdx; i < EndIdx && i < N; ++i)
				{
					if (i == 0 || i == N - 1)
					{
						continue;
					}
					Vertices[i] = Buffer[i];
				}
			}
		}
	}
};

