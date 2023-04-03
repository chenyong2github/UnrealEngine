// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Criteria/CriteriaGrid.h"

#include "CADKernel/Mesh/Criteria/Criterion.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/UI/Display.h"

#ifdef CADKERNEL_DEV
#include "CADKernel/UI/DefineForDebug.h"
#endif

namespace UE::CADKernel
{

void FCriteriaGrid::Init()
{
	TFunction<void(EIso)> ComputeMiddlePointsCoordinates = [&](EIso Iso)
	{
		const TArray<double>& Tab = Face.GetCrossingPointCoordinates(Iso);
		TArray<double>& Tab2 = CoordinateGrid[Iso];

		CuttingCount[Iso] = Tab.Num() * 2 - 1;
		ensure(CuttingCount[Iso]);
		Tab2.SetNum(CuttingCount[Iso]);

		Tab2[0] = Tab[0];
		for (int32 Index = 1; Index < Tab.Num(); Index++)
		{
			Tab2[2 * Index - 1] = (Tab[Index - 1] + Tab[Index]) / 2.0;
			Tab2[2 * Index] = Tab[Index];
		}
	};

	ComputeMiddlePointsCoordinates(EIso::IsoU);
	ComputeMiddlePointsCoordinates(EIso::IsoV);

	EvaluatePointGrid(CoordinateGrid, false);
}

FCriteriaGrid::FCriteriaGrid(FTopologicalFace& InFace)
	: FGridBase(InFace)
	, CoordinateGrid(InFace.GetCrossingPointCoordinates())
{
	Init();
#ifdef DISPLAY_CRITERIA_GRID
	Display();
#endif
}

} // namespace UE::CADKernel

