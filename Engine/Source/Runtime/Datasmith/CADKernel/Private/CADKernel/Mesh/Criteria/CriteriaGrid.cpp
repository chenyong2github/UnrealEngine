// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Criteria/CriteriaGrid.h"

#include "CADKernel/Mesh/Criteria/Criterion.h"

#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/UI/Display.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/DefineForDebug.h"

using namespace CADKernel;


const FPoint& FCriteriaGrid::GetPoint(int32 UIndex, int32 VIndex, bool bIsInternalU, bool bIsInternalV, FVector* OutNormal) const
{
	int32 Index = GetIndex(UIndex, VIndex, bIsInternalU, bIsInternalV);
	ensureCADKernel(Grid.Points3D.IsValidIndex(Index));

	if (OutNormal)
	{
		*OutNormal = Grid.Normals[Index];
	}
	return Grid.Points3D[Index];
}

void FCriteriaGrid::Init()
{
	TFunction<void(const TArray<double>&, TArray<double>&)> ComputeMiddlePointsCoordinates = [](const TArray<double>& Tab, TArray<double>& Tab2)
	{
		Tab2.SetNum(Tab.Num() * 2 - 1);
		Tab2[0] = Tab[0];
		for (int32 Index = 1; Index < Tab.Num(); Index++)
		{
			Tab2[2 * Index - 1] = (Tab[Index - 1] + Tab[Index]) / 2.0;
			Tab2[2 * Index] = Tab[Index];
		}
	};

	FCoordinateGrid CoordinateGrid2;
	ComputeMiddlePointsCoordinates(Surface->GetCrossingPointCoordinates(EIso::IsoU), CoordinateGrid2[EIso::IsoU]);
	ComputeMiddlePointsCoordinates(Surface->GetCrossingPointCoordinates(EIso::IsoV), CoordinateGrid2[EIso::IsoV]);

	Surface->GetCarrierSurface()->EvaluatePointGrid(CoordinateGrid2, Grid, true);
	TrueUcoorindateCount = CoordinateGrid2[EIso::IsoU].Num();
}

FCriteriaGrid::FCriteriaGrid(TSharedRef<FTopologicalFace> InSurface)
	: Surface(InSurface)
	, CoordinateGrid(InSurface->GetCrossingPointCoordinates())
{
	Surface->Presample();
	Surface->InitDeltaUs();
	Init();
#ifdef DISPLAY_CRITERIA_GRID
	Display();
#endif
}

void FCriteriaGrid::ApplyCriteria(const TArray<TSharedPtr<FCriterion>>& Criteria) const
{
	TArray<double>& DeltaUMaxArray = Surface->GetCrossingPointDeltaMaxs(EIso::IsoU);
	TArray<double>& DeltaUMiniArray = Surface->GetCrossingPointDeltaMins(EIso::IsoU);
	TArray<double>& DeltaVMaxArray = Surface->GetCrossingPointDeltaMaxs(EIso::IsoV);
	TArray<double>& DeltaVMinArray = Surface->GetCrossingPointDeltaMins(EIso::IsoV);
	FSurfaceCurvature& SurfaceCurvature = Surface->GetCurvatures();

	for (int32 IndexV = 0; IndexV < GetCoordinateCount(EIso::IsoV) - 1; ++IndexV)
	{
		for (int32 IndexU = 0; IndexU < GetCoordinateCount(EIso::IsoU) - 1; ++IndexU)
		{
			const FPoint& Point = GetPoint(IndexU, IndexV);
			const FPoint& PointU = GetPoint(IndexU + 1, IndexV);
			const FPoint& PointV = GetPoint(IndexU, IndexV + 1);
			const FPoint& PointUV = GetPoint(IndexU + 1, IndexV + 1);
			const FPoint& PointUMid = GetIntermediateU(IndexU, IndexV);
			const FPoint& PointVMid = GetIntermediateV(IndexU, IndexV);
			const FPoint& PointUVMid = GetIntermediateUV(IndexU, IndexV);

			// Evaluate Sag
			double LengthU;
			double SagU = FCriterion::EvaluateSag(Point, PointU, PointUMid, LengthU);
			double LengthV;
			double SagV = FCriterion::EvaluateSag(Point, PointV, PointVMid, LengthV);
			double LengthUV;
			double SagUV = FCriterion::EvaluateSag(Point, PointUV, PointUVMid, LengthUV);

			for (const TSharedPtr<FCriterion>& Criterion : Criteria)
			{
				Criterion->UpdateDelta((GetCoordinate(EIso::IsoU, IndexU + 1) - GetCoordinate(EIso::IsoU, IndexU)), SagU, SagUV, SagV, LengthU, LengthUV, DeltaUMaxArray[IndexU], DeltaUMiniArray[IndexU], SurfaceCurvature[EIso::IsoU]);
				Criterion->UpdateDelta((GetCoordinate(EIso::IsoV, IndexV + 1) - GetCoordinate(EIso::IsoV, IndexV)), SagV, SagUV, SagU, LengthV, LengthUV, DeltaVMaxArray[IndexV], DeltaVMinArray[IndexV], SurfaceCurvature[EIso::IsoV]);
			}
		}
	}

	// Delta of the extremities are smooth to avoid big disparity 
	if (DeltaUMaxArray.Num() > 2)
	{
		DeltaUMaxArray[0] = (DeltaUMaxArray[0] + DeltaUMaxArray[1] * 2) * AThird;
		DeltaUMaxArray.Last() = (DeltaUMaxArray.Last() + DeltaUMaxArray[DeltaUMaxArray.Num() - 2] * 2) * AThird;
	}

	if (DeltaVMaxArray.Num() > 2)
	{
		DeltaVMaxArray[0] = (DeltaVMaxArray[0] + DeltaVMaxArray[1] * 2) * AThird;
		DeltaVMaxArray.Last() = (DeltaVMaxArray.Last() + DeltaVMaxArray[DeltaVMaxArray.Num() - 2] * 2) * AThird;
	}
}

void FCriteriaGrid::Display()
{
	Open3DDebugSession(TEXT("Grid"));

	Open3DDebugSession(TEXT("CriteriaGrid Point 3d"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(GetPoint(UIndex, VIndex), UIndex);
		}
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("CriteriaGrid IntermediateU"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU) - 1; ++UIndex)
		{
			CADKernel::DisplayPoint(GetIntermediateU(UIndex, VIndex), EVisuProperty::ControlPoint);
		}
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("CriteriaGrid IntermediateV"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV) - 1; ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(GetIntermediateV(UIndex, VIndex), EVisuProperty::ControlPoint);
		}
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("CriteriaGrid IntermediateUV"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV) - 1; ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU) - 1; ++UIndex)
		{
			CADKernel::DisplayPoint(GetIntermediateUV(UIndex, VIndex), EVisuProperty::ControlPoint);
		}
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("Loop 3D"));
	for (const TSharedPtr<FTopologicalLoop>& Loop : Surface->GetLoops())
	{
		::Display(Loop);
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("Loop 2D"));
	for (const TSharedPtr<FTopologicalLoop>& Loop : Surface->GetLoops())
	{
		::Display2D(Loop);
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("CriteriaGrid Point 2D"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(FPoint2D(GetCoordinate(EIso::IsoU, UIndex), GetCoordinate(EIso::IsoV, VIndex)));
		}
	}
	Close3DDebugSession();

	Open3DDebugSession(TEXT("CriteriaGrid Point 2D Intermediate"));
	for (int32 VIndex = 0; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 1; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(FPoint2D((GetCoordinate(EIso::IsoU, UIndex) + GetCoordinate(EIso::IsoU, UIndex - 1))*0.5, GetCoordinate(EIso::IsoV, VIndex)), EVisuProperty::ControlPoint);
		}
	}
	for (int32 VIndex = 1; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 0; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(FPoint2D(GetCoordinate(EIso::IsoU, UIndex), (GetCoordinate(EIso::IsoV, VIndex) + GetCoordinate(EIso::IsoV, VIndex - 1))*0.5), EVisuProperty::ControlPoint);
		}
	}
	for (int32 VIndex = 1; VIndex < GetCoordinateCount(EIso::IsoV); ++VIndex)
	{
		for (int32 UIndex = 1; UIndex < GetCoordinateCount(EIso::IsoU); ++UIndex)
		{
			CADKernel::DisplayPoint(FPoint2D((GetCoordinate(EIso::IsoU, UIndex) + GetCoordinate(EIso::IsoU, UIndex - 1))*0.5, (GetCoordinate(EIso::IsoV, VIndex) + GetCoordinate(EIso::IsoV, VIndex - 1))*0.5), EVisuProperty::ControlPoint);
		}
	}
	Close3DDebugSession();

	Close3DDebugSession();
}

