// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Curves/SegmentCurve.h"


TSharedPtr<CADKernel::FEntityGeom> CADKernel::FSegmentCurve::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FPoint TransformedStartPoint = InMatrix.Multiply(StartPoint);
	FPoint TransformedEndPoint = InMatrix.Multiply(EndPoint);

	return FEntity::MakeShared<FSegmentCurve>(TransformedStartPoint, TransformedEndPoint, Dimension);
}

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FSegmentCurve::GetInfo(FInfoEntity& Info) const
{
	return FCurve::GetInfo(Info).Add(TEXT("StartPoint"), StartPoint).Add(TEXT("pt2"), EndPoint);
}
#endif

