// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvexHull3.h"

#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteConvexHull3.h"




template <typename RealType> 
struct TConvexHull3Internal
{
	using PreciseNumberType = gte::BSNumber<gte::UIntegerFP32<197>>;		// 197 is from ConvexHull3 documentation
	using DVector3 = gte::Vector3<double>;

	bool bUseExact;
	TArray<DVector3> DoubleInput;

	void SetPoint(int32 Index, const FVector3<RealType>& Point)
	{
		DoubleInput[Index] = DVector3{ {(double)Point.X, (double)Point.Y, (double)Point.Z} };
	}

	gte::ConvexHull3<double, double> DoubleCompute;
	gte::ConvexHull3<double, PreciseNumberType> PreciseCompute;
	bool bSolutionOK = false;

	bool ComputeResult()
	{
		bSolutionOK = false;
		if (bUseExact)
		{
			bSolutionOK = PreciseCompute(DoubleInput.Num(), &DoubleInput[0], 0.0);
		}
		else
		{
			bSolutionOK = DoubleCompute(DoubleInput.Num(), &DoubleInput[0], 0.0);
		}
		return bSolutionOK;
	}


	void EnumerateTriangles( TFunctionRef<void(FIndex3i)> TriangleFunc )
	{
		ensure(bSolutionOK);
		std::vector<gte::TriangleKey<true>> const& Triangles = (bUseExact) ?
			PreciseCompute.GetHullUnordered() : DoubleCompute.GetHullUnordered();

		for (const gte::TriangleKey<true>& Tri : Triangles)
		{
			TriangleFunc(FIndex3i(Tri.V[0], Tri.V[1], Tri.V[2]));
		}
	}
};



template<typename RealType>
bool TConvexHull3<RealType>::Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactComputation)
{
	Initialize(NumPoints, bUseExactComputation);
	check(Internal);

	for (int32 k = 0; k < NumPoints; ++k)
	{
		FVector3<RealType> Point = GetPointFunc(k);
		Internal->SetPoint(k, Point);
	}

	return Internal->ComputeResult();
}

template<typename RealType>
bool TConvexHull3<RealType>::IsSolutionAvailable() const
{
	return Internal && Internal->bSolutionOK;
}

template<typename RealType>
void TConvexHull3<RealType>::GetTriangles(TFunctionRef<void(FIndex3i)> TriangleFunc)
{
	ensure(IsSolutionAvailable());
	Internal->EnumerateTriangles(TriangleFunc);

}


template<typename RealType>
void TConvexHull3<RealType>::Initialize(int32 NumPoints, bool bUseExactComputation)
{
	Internal = MakePimpl<TConvexHull3Internal<RealType>>();

	Internal->bUseExact = bUseExactComputation;
	Internal->DoubleInput.SetNum(NumPoints);
}







template class GEOMETRYALGORITHMS_API TConvexHull3<float>;
template class GEOMETRYALGORITHMS_API TConvexHull3<double>;