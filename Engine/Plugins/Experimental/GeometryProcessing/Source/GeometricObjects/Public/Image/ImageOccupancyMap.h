// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Tuple.h"
#include "VectorTypes.h"
#include "Image/ImageDimensions.h"
#include "Spatial/MeshAABBTree3.h"

/**
 * ImageOccupancyMap calculates and stores coverage information for a 2D image/texture,
 * for example coverage derived from UV islands of a mesh, 2D polygons, etc.
 * 
 * An optional set of gutter texels can be calculated, and correspondence between gutter
 * texels and the nearest interior texel is stored.
 *
 * In addition, a 2D coordinate (eg UV) and integer ID (eg Triangle ID) of each texel can be calculated/stored. 
 * This is not just a cache. For 'border' texels where the texel center is technically outside the mesh/polygon,
 * but the texel rectangle may still overlap the shape, the nearest UV/Triangle is stored.
 * This simplifies computing samples around the borders such that the shape is covered under linear interpolatione/etc.
 */
class FImageOccupancyMap
{
public:
	/** Image Dimensions */
	FImageDimensions Dimensions;

	/** Width of the gutter. This is actually multiplied by the diagonal length of a texel, so
	    the gutter is generally larger than this number of pixels */
	int32 GutterSize = 4;

	// texel types
	const int8 EmptyTexel = 0;
	const int8 InteriorTexel = 1;
	//const int8 BorderTexel = 2;
	const int8 GutterTexel = 3;

	/** Texel Type for each texel in image, Size = Width x Height */
	TArray64<int8> TexelType;

	/** UV for each texel in image. Only set for Interior texels */
	TArray64<FVector2f> TexelQueryUV;
	/** integer/Triangle ID for each texel in image. Only set for Interior texels. */
	TArray64<int32> TexelQueryTriangle;

	/** Set of Gutter Texels. Pair is <LinearIndexOfGutterTexel, LinearIndexOfNearestInteriorTexel>, so
	    Gutter can be filled by directly copying from source to target. */
	TArray64<TTuple<int64, int64>> GutterTexels;


	void Initialize(FImageDimensions DimensionsIn)
	{
		check(DimensionsIn.IsSquare()); // are we sure it works otherwise?
		Dimensions = DimensionsIn;
	}


	/**
	 * @return true if texel at this linear index is an Interior texel
	 */
	bool IsInterior(int64 LinearIndex) const
	{
		return TexelType[LinearIndex] == InteriorTexel;
	}

	/** Set texel type at the given X/Y */
	//void SetTexelType(int64 X, int64 Y, int8 Type)
	//{
	//	TexelType[Dimensions.GetIndex(X, Y)] = Type;
	//}


	template<typename MeshType, typename GetTriangleIDFuncType>
	bool ComputeFromUVSpaceMesh(const MeshType& UVSpaceMesh, 
		GetTriangleIDFuncType GetTriangleIDFunc = [](int32 TriangleID) { return TriangleID; } )
	{
		// make flat mesh
		TMeshAABBTree3<MeshType> FlatSpatial(&UVSpaceMesh, true);

		TexelType.Init(EmptyTexel, Dimensions.Num());
		TexelQueryUV.Init(FVector2f::Zero(), Dimensions.Num());
		TexelQueryTriangle.Init(IndexConstants::InvalidID, Dimensions.Num());

		FVector2d TexelDims = Dimensions.GetTexelSize();
		double TexelDiag = TexelDims.Length();

		IMeshSpatial::FQueryOptions QueryOptions;
		QueryOptions.MaxDistance = (double)GutterSize * TexelDiag;

		// find interior texels
		TAtomic<int64> InteriorCounter;
		FCriticalSection GutterLock;
		ParallelFor(Dimensions.Num(), [&](int64 LinearIdx)
		{
			FVector2d UVPoint = Dimensions.GetTexelUV(LinearIdx);
			FVector3d UVPoint3d(UVPoint.X, UVPoint.Y, 0);

			double NearDistSqr;
			int32 NearestTriID = FlatSpatial.FindNearestTriangle(UVPoint3d, NearDistSqr, QueryOptions);
			if (NearestTriID >= 0)
			{
				FVector3d A, B, C;
				UVSpaceMesh.GetTriVertices(NearestTriID, A, B, C);
				FTriangle2d UVTriangle(A.XY(), B.XY(), C.XY());

				if (UVTriangle.IsInsideOrOn(UVPoint))
				{
					TexelType[LinearIdx] = InteriorTexel;
					TexelQueryUV[LinearIdx] = (FVector2f)UVPoint;
					TexelQueryTriangle[LinearIdx] = GetTriangleIDFunc(NearestTriID);
					InteriorCounter.IncrementExchange();
				}
				else if (NearDistSqr < TexelDiag * TexelDiag)
				{
					FDistPoint3Triangle3d DistQuery = TMeshQueries<MeshType>::TriangleDistance(UVSpaceMesh, NearestTriID, UVPoint3d);
					FVector2d NearestUV = DistQuery.ClosestTrianglePoint.XY();
					// nudge point into triangle to improve numerical behavior of things like barycentric coord calculation
					NearestUV += (10.0 * FMathf::ZeroTolerance) * (NearestUV - UVPoint).Normalized();

					TexelType[LinearIdx] = InteriorTexel;
					TexelQueryUV[LinearIdx] = (FVector2f)NearestUV;
					TexelQueryTriangle[LinearIdx] = GetTriangleIDFunc(NearestTriID);
				}
				else
				{
					TexelType[LinearIdx] = GutterTexel;
					FDistPoint3Triangle3d DistQuery = TMeshQueries<MeshType>::TriangleDistance(UVSpaceMesh, NearestTriID, UVPoint3d);
					FVector2d NearestUV = DistQuery.ClosestTrianglePoint.XY();
					FVector2i NearestCoords = Dimensions.UVToCoords(NearestUV);
					int64 NearestLinearIdx = Dimensions.GetIndex(NearestCoords);

					GutterLock.Lock();
					GutterTexels.Add(TTuple<int64, int64>(LinearIdx, NearestLinearIdx));
					GutterLock.Unlock();
				}
			}
		});

		return true;
	}




	template<typename TexelValueType>
	void ParallelProcessingPass(
		TFunctionRef<TexelValueType(int64 LinearIdx)> BeginTexel,
		TFunctionRef<void(int64 LinearIdx, float Weight, TexelValueType&)> AccumulateTexel,
		TFunctionRef<void(int64 LinearIdx, float Weight, TexelValueType&)> CompleteTexel,
		TFunctionRef<void(int64 LinearIdx, TexelValueType&)> WriteTexel,
		TFunctionRef<float(const FVector2i& TexelOffset)> WeightFunction,
		int32 FilterWidth,
		TArray<TexelValueType>& PassBuffer
	) const
	{
		int64 N = Dimensions.Num();
		PassBuffer.SetNum(N);

		ParallelFor(N, [&](int64 LinearIdx)
		{
			if (TexelType[LinearIdx] != EmptyTexel)
			{
				TexelValueType AccumValue = BeginTexel(LinearIdx);
				float WeightSum = 0;

				FVector2i Coords = Dimensions.GetCoords(LinearIdx);
				FVector2i MaxNbr = Coords + FVector2i(FilterWidth, FilterWidth);
				Dimensions.Clamp(MaxNbr);
				FVector2i MinNbr = Coords - FVector2i(FilterWidth, FilterWidth);
				Dimensions.Clamp(MinNbr);

				for (int32 Y = MinNbr.Y; Y <= MaxNbr.Y; ++Y)
				{
					for (int32 X = MinNbr.X; X <= MaxNbr.X; ++X)
					{
						FVector2i NbrCoords(X, Y);
						if (Dimensions.IsValidCoords(NbrCoords))
						{
							FVector2i Offset = NbrCoords - Coords;
							int64 LinearNbrIndex = Dimensions.GetIndex(NbrCoords);
							if (TexelType[LinearNbrIndex] != EmptyTexel)
							{
								float NbrWeight = WeightFunction(Offset);
								AccumulateTexel(LinearNbrIndex, NbrWeight, AccumValue);
								WeightSum += NbrWeight;
							}
						}
					}
				}

				CompleteTexel(LinearIdx, WeightSum, AccumValue);
				PassBuffer[LinearIdx] = AccumValue;
			}
		});

		// write results
		for (int64 k = 0; k < N; ++k)
		{
			if (TexelType[k] != EmptyTexel)
			{
				WriteTexel(k, PassBuffer[k]);
			}
		}
	}


};


