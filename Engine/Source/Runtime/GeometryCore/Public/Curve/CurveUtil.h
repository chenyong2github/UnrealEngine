// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "SegmentTypes.h"
#include "LineTypes.h"
#include "BoxTypes.h"
#include "SegmentTypes.h"

namespace UE
{
namespace Geometry
{
namespace CurveUtil
{

using namespace UE::Math;

/**
 * Curve utility functions
 */
	template<typename RealType, typename VectorType>
	TVector<RealType> Tangent(const TArrayView<const VectorType>& Vertices, int32 Idx, bool bLoop = false)
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
		return Normalized(Vertices[EndIdx] - Vertices[StartIdx]);
	}


	template<typename RealType, typename VectorType>
	static RealType ArcLength(const TArrayView<const VectorType>& Vertices, bool bLoop = false) {
		RealType Sum = 0;
		int32 NV = Vertices.Num();
		for (int i = 1; i < NV; ++i)
		{
			Sum += Distance(Vertices[i], Vertices[i-1]);
		}
		if (bLoop)
		{
			Sum += Distance(Vertices[NV-1], Vertices[0]);
		}
		return Sum;
	}

	template<typename RealType, typename VectorType>
	int FindNearestIndex(const TArrayView<const VectorType>& Vertices, const VectorType& V)
	{
		int iNearest = -1;
		double dNear = TMathUtil<double>::MaxReal;
		int N = Vertices.Num();
		for ( int i = 0; i < N; ++i )
		{
			double dSqr = DistanceSquared(Vertices[i], V);
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
	template<typename RealType, typename VectorType>
	void InPlaceSmooth(TArrayView<VectorType> Vertices, int StartIdx, int EndIdx, double Alpha, int NumIterations, bool bClosed)
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
					TVector<RealType> prev = Vertices[iPrev], next = Vertices[iNext];
					TVector<RealType> c = (prev + next) * 0.5f;
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
					TVector<RealType> prev = Vertices[i - 1], next = Vertices[i + 1];
					TVector<RealType> c = (prev + next) * 0.5f;
					Vertices[i] = (1 - Alpha) * Vertices[i] + (Alpha) * c;
				}
			}
		}
	}



	/**
	 * smooth set of vertices using extra buffer
	 */
	template<typename RealType, typename VectorType>
	void IterativeSmooth(TArrayView<VectorType> Vertices, int StartIdx, int EndIdx, double Alpha, int NumIterations, bool bClosed)
	{
		int N = Vertices.Num();
		TArray<TVector<RealType>> Buffer;
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
					TVector<RealType> prev = Vertices[iPrev], next = Vertices[iNext];
					TVector<RealType> c = (prev + next) * 0.5f;
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
					TVector<RealType> prev = Vertices[i - 1], next = Vertices[i + 1];
					TVector<RealType> c = (prev + next) * 0.5f;
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


} // end namespace UE::Geometry::CurveUtil
} // end namespace UE::Geometry
} // end namespace UE