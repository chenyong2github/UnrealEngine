// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvexHull2.h"

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteConvexHull2.h"
THIRD_PARTY_INCLUDES_END

#include "GteUtil.h"




template <typename RealType>
struct TConvexHull2Internal
{
	using PreciseNumberType = gte::BSNumber<gte::UIntegerFP32<132>>;		// 132 is from ConvexHull2 documentation
	using DVector2 = gte::Vector2<double>;

	bool bUseExact;
	TArray<DVector2> DoubleInput;

	TPolygon2<RealType> ConvexPolygonResult;
	bool bSolutionOK;

	void SetPoint(int32 Index, const FVector2<RealType>& Point)
	{
		DoubleInput[Index] = DVector2{ {(double)Point.X, (double)Point.Y} };
	}

	bool ComputeResult()
	{
		bSolutionOK = false;
		if (bUseExact)
		{
			gte::ConvexHull2<double, PreciseNumberType> PreciseCompute;
			bSolutionOK = PreciseCompute(DoubleInput.Num(), &DoubleInput[0], 0.0);
			if (bSolutionOK)
			{
				MakePolygon(PreciseCompute.GetHull());
			}
		}
		else
		{
			gte::ConvexHull2<double, double> DoubleCompute;
			bSolutionOK = DoubleCompute(DoubleInput.Num(), &DoubleInput[0], 0.0);
			if (bSolutionOK)
			{
				MakePolygon(DoubleCompute.GetHull());
			}
		}
		return bSolutionOK;
	}

	void MakePolygon(const std::vector<int>& HullVertices)
	{
		for (int32 k = 0; k < HullVertices.size(); ++k)
		{
			DVector2 Point = DoubleInput[HullVertices[k]];
			ConvexPolygonResult.AppendVertex( (FVector2<RealType>)Convert(Point) );
		}
	}

};



template<typename RealType>
bool TConvexHull2<RealType>::Solve(int32 NumPoints, TFunctionRef<FVector2<RealType>(int32)> GetPointFunc, bool bUseExactComputation)
{
	Initialize(NumPoints, bUseExactComputation);
	check(Internal);

	for (int32 k = 0; k < NumPoints; ++k)
	{
		FVector2<RealType> Point = GetPointFunc(k);
		Internal->SetPoint(k, Point);
	}

	return Internal->ComputeResult();
}

template<typename RealType>
bool TConvexHull2<RealType>::IsSolutionAvailable() const
{
	return Internal && Internal->bSolutionOK;
}

template<typename RealType>
void TConvexHull2<RealType>::GetPolygon(TPolygon2<RealType>& PolygonOut) const
{
	ensure(IsSolutionAvailable());
	PolygonOut = Internal->ConvexPolygonResult;

}


template<typename RealType>
void TConvexHull2<RealType>::Initialize(int32 NumPoints, bool bUseExactComputation)
{
	Internal = MakePimpl<TConvexHull2Internal<RealType>>();

	Internal->bUseExact = bUseExactComputation;
	Internal->DoubleInput.SetNum(NumPoints);
}



template class GEOMETRYALGORITHMS_API TConvexHull2<float>;
template class GEOMETRYALGORITHMS_API TConvexHull2<double>;