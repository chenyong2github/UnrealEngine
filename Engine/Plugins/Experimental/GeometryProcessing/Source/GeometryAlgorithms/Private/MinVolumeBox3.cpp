// Copyright Epic Games, Inc. All Rights Reserved.

#include "MinVolumeBox3.h"

#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteBSRational.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerAP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteConvexHull3.h"
#include "ThirdParty/GTEngine/Mathematics/GteMinimumVolumeBox3.h"



template <typename RealType>
struct TMinVolumeBox3Internal
{
	using PreciseHullNumberType = gte::BSNumber<gte::UIntegerFP32<197>>;		// 197 is from ConvexHull3 documentation
	using PreciseBoxNumberType = gte::BSRational<gte::UIntegerAP32>;
	using DVector3 = gte::Vector3<double>;

	bool bUseExact;
	TArray<DVector3> DoubleInput;

	TOrientedBox3<RealType> Result;
	bool bSolutionOK;

	void SetPoint(int32 Index, const FVector3<RealType>& Point)
	{
		DoubleInput[Index] = DVector3{ {(double)Point.X, (double)Point.Y, (double)Point.Z} };
	}

	bool ComputeResult()
	{
		gte::OrientedBox3<double> MinimalBox;

		if (bUseExact)
		{
			// compute convex hull 
			gte::ConvexHull3<double, PreciseHullNumberType> HullCompute;
			bSolutionOK = HullCompute(DoubleInput.Num(), &DoubleInput[0], 0.0);
			if (bSolutionOK)
			{
				// compute minimal box for convex hull
				std::vector<gte::TriangleKey<true>> const& HullTriangles = HullCompute.GetHullUnordered();
				int const numIndices = static_cast<int>(3 * HullTriangles.size());
				int const* indices = static_cast<int const*>(&HullTriangles[0].V[0]);
				gte::MinimumVolumeBox3<double, PreciseBoxNumberType> BoxCompute;
				MinimalBox = BoxCompute(DoubleInput.Num(), &DoubleInput[0], numIndices, indices);
			}
		}
		else
		{
			gte::MinimumVolumeBox3<double, double> DoubleCompute;
			MinimalBox = DoubleCompute(DoubleInput.Num(), &DoubleInput[0], true);
			bSolutionOK = true;
		}

		Result.Frame = TFrame3<RealType>(
			FVector3<RealType>((RealType)MinimalBox.center[0], (RealType)MinimalBox.center[1], (RealType)MinimalBox.center[2]),
			FVector3<RealType>((RealType)MinimalBox.axis[0][0], (RealType)MinimalBox.axis[0][1], (RealType)MinimalBox.axis[0][2]),
			FVector3<RealType>((RealType)MinimalBox.axis[1][0], (RealType)MinimalBox.axis[1][1], (RealType)MinimalBox.axis[1][2]),
			FVector3<RealType>((RealType)MinimalBox.axis[2][0], (RealType)MinimalBox.axis[2][1], (RealType)MinimalBox.axis[2][2]) );
		Result.Extents = FVector3<RealType>((RealType)MinimalBox.extent[0], (RealType)MinimalBox.extent[1], (RealType)MinimalBox.extent[2]);

		return true;
	}

};



template<typename RealType>
bool TMinVolumeBox3<RealType>::Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactComputation)
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
bool TMinVolumeBox3<RealType>::IsSolutionAvailable() const
{
	return Internal && Internal->bSolutionOK;
}

template<typename RealType>
void TMinVolumeBox3<RealType>::GetResult(TOrientedBox3<RealType>& BoxOut)
{
	ensure(IsSolutionAvailable());
	BoxOut = Internal->Result;
}


template<typename RealType>
void TMinVolumeBox3<RealType>::Initialize(int32 NumPoints, bool bUseExactComputation)
{
	// Currently this technique fails with non-exact computation, so you need to use exact
	check(bUseExactComputation == true);

	Internal = MakePimpl<TMinVolumeBox3Internal<RealType>>();
	Internal->bUseExact = bUseExactComputation;
	Internal->DoubleInput.SetNum(NumPoints);
}



template class GEOMETRYALGORITHMS_API TMinVolumeBox3<float>;
template class GEOMETRYALGORITHMS_API TMinVolumeBox3<double>;