// Copyright Epic Games, Inc. All Rights Reserved.

#include "MinVolumeBox3.h"

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 28020)		// disable this warning that occurs in GteMinimumVolumeBox3.h
#endif
THIRD_PARTY_INCLUDES_START
#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteBSRational.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerAP32.h"
#include "ThirdParty/GTEngine/Mathematics/GteConvexHull3.h"
#include "ThirdParty/GTEngine/Mathematics/GteMinimumVolumeBox3.h"
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif

#include "GteUtil.h"

template <typename RealType>
struct TMinVolumeBox3Internal
{
	using PreciseHullNumberType = gte::BSNumber<gte::UIntegerFP32<197>>;		// 197 is from ConvexHull3 documentation
	using PreciseBoxNumberType = gte::BSRational<gte::UIntegerAP32>;
	using DVector3 = gte::Vector3<double>;

	bool bUseExactHull;
	bool bUseExactBox;
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

		if (bUseExactHull)
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
				if (bUseExactBox)
				{
					gte::MinimumVolumeBox3<double, PreciseBoxNumberType> BoxCompute;
					MinimalBox = BoxCompute(DoubleInput.Num(), &DoubleInput[0], numIndices, indices);
				}
				else
				{
					gte::MinimumVolumeBox3<double, double> BoxCompute;
					MinimalBox = BoxCompute(DoubleInput.Num(), &DoubleInput[0], numIndices, indices);
				}
			}
		}
		else
		{
			gte::MinimumVolumeBox3<double, double> DoubleCompute;
			MinimalBox = DoubleCompute(DoubleInput.Num(), &DoubleInput[0], true);
			bSolutionOK = true;
		}

		// if resulting box is not finite, something went wrong, just return an empty box
		FVector3d Extents = Convert(MinimalBox.extent);
		if (!FMathd::IsFinite(Extents.SquaredLength()))
		{
			bSolutionOK = false;
			MinimalBox = gte::OrientedBox3<double>();
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
bool TMinVolumeBox3<RealType>::Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactHull, bool bUseExactBox)
{
	Initialize(NumPoints, bUseExactHull, bUseExactBox);
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
void TMinVolumeBox3<RealType>::Initialize(int32 NumPoints, bool bUseExactHull, bool bUseExactBox)
{
	Internal = MakePimpl<TMinVolumeBox3Internal<RealType>>();
	Internal->bUseExactHull = bUseExactHull;
	Internal->bUseExactBox = bUseExactBox;
	Internal->DoubleInput.SetNum(NumPoints);
}



template class GEOMETRYALGORITHMS_API TMinVolumeBox3<float>;
template class GEOMETRYALGORITHMS_API TMinVolumeBox3<double>;