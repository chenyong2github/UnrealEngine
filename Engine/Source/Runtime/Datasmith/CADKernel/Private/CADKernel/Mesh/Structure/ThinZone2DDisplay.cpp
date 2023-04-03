// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Mesh/Structure/ThinZone2DFinder.h"

#ifdef CADKERNEL_DEBUG

#include "CADKernel/Math/Point.h"
#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Mesh/Structure/EdgeSegment.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/UI/Display.h"

namespace UE::CADKernel
{

//#define DEBUG_SPLIT_BY_EDGE
void FThinZone2DFinder::DisplayLoopSegments()
{
	if (!bDisplay)
	{
		return;
	}

	FTopologicalEdge* currentEdge = LoopSegments[0]->GetEdge();

	F3DDebugSession _(TEXT("BoundarySegment"));

#ifdef DEBUG_SPLIT_BY_EDGE
	F3DDebugSession E(FString::Printf(TEXT("Edge %d"), currentEdge->GetId()));
#endif
	for (FEdgeSegment* EdgeSegment : LoopSegments)
	{
#ifdef DEBUG_SPLIT_BY_EDGE
		if (currentEdge != EdgeSegment->GetEdge())
		{
			currentEdge = EdgeSegment->GetEdge();
			Close3DDebugSession();
			Open3DDebugSession(FString::Printf(TEXT("Edge %d"), currentEdge->GetId()));
		}
#endif
		ThinZone::DisplayEdgeSegment(EdgeSegment, EVisuProperty::GreenCurve);
	}
	Wait();
}

void FThinZone2DFinder::DisplayCloseSegments()
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(TEXT("Close Segments"));
	for (const FEdgeSegment* Segment : LoopSegments)
	{
		if (Segment->GetCloseSegment())
		{
			ThinZone::DisplayEdgeSegmentAndProjection(Segment, EVisuProperty::BlueCurve, EVisuProperty::BlueCurve, EVisuProperty::RedCurve);
		}
	}
}

void ThinZone::DisplayEdgeSegmentAndProjection(const FEdgeSegment* Segment, EVisuProperty SegColor, EVisuProperty OppositeColor, EVisuProperty ProjectionColor)
{
	FEdgeSegment* CloseSegment = Segment->GetCloseSegment();
	DisplayEdgeSegmentAndProjection(Segment, CloseSegment, SegColor, OppositeColor, ProjectionColor);
}

void ThinZone::DisplayEdgeSegmentAndProjection(const FEdgeSegment* Segment, const FEdgeSegment* CloseSegment, EVisuProperty SegColor, EVisuProperty OppositeColor, EVisuProperty ProjectionColor)
{
	DisplayEdgeSegment(Segment, SegColor);
	DisplayEdgeSegment(CloseSegment, OppositeColor);

	double Coordinate;
	FPoint2D Projection = ProjectPointOnSegment(Segment->GetCenter(), CloseSegment->GetExtemity(ELimit::Start), CloseSegment->GetExtemity(ELimit::End), Coordinate, true);

	DisplaySegmentWithScale(Projection, Segment->GetCenter(), Segment->GetId(), ProjectionColor);
}

void ThinZone::DisplayEdgeSegment(const FEdgeSegment* EdgeSegment, EVisuProperty Color, int32 Index)
{
	DisplaySegmentWithScale(EdgeSegment->GetExtemity(ELimit::Start), EdgeSegment->GetExtemity(ELimit::End), Index, Color);
}

void ThinZone::DisplayEdgeSegment(const FEdgeSegment* EdgeSegment, EVisuProperty Color)
{
	DisplayEdgeSegment(EdgeSegment, Color, EdgeSegment->GetId());
}

void ThinZone::DisplayThinZoneSide(const TArray<FEdgeSegment*>& Side, int32 Index, EVisuProperty Color, bool bSplitBySegment)
{
	F3DDebugSession A(FString::Printf(TEXT("Side %d"), Index));
	for (const FEdgeSegment* EdgeSegment : Side)
	{
		F3DDebugSession A(bSplitBySegment, TEXT("Seg"));
		DisplayEdgeSegment(EdgeSegment, Color);
	}
}

void ThinZone::DisplayThinZoneSide(const FThinZoneSide& Side, int32 Index, EVisuProperty Color, bool bSplitBySegment)
{
	F3DDebugSession A(FString::Printf(TEXT("Side %d"), Index));
	for (const FEdgeSegment& EdgeSegment : Side.GetSegments())
	{
		F3DDebugSession A(bSplitBySegment, TEXT("Seg"));
		DisplayEdgeSegment(&EdgeSegment, Color);
	}
}

static EVisuProperty GetRandomColor(int32 Index)
{
	static TArray<EVisuProperty> ZoneColor;
	ZoneColor.Add(EVisuProperty::OrangeCurve);
	ZoneColor.Add(EVisuProperty::BlueCurve);
	ZoneColor.Add(EVisuProperty::GreenCurve);
	ZoneColor.Add(EVisuProperty::PurpleCurve);
	ZoneColor.Add(EVisuProperty::RedCurve);
	ZoneColor.Add(EVisuProperty::YellowCurve);
	const uint32 ColorCount = ZoneColor.Num();

	const int32 ColorIndex = Index % ColorCount;
	return ZoneColor[ColorIndex];
}

void FThinZone2DFinder::DisplaySegmentsOfThinZone()
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(TEXT("Segments Of ThinZones"));

	FIdent LastIndex = Ident::Undefined - 1;
	EVisuProperty Color;

	for (FEdgeSegment* EdgeSegment : LoopSegments)
	{
		FIdent ChainIndex = EdgeSegment->GetChainIndex();
		if (ChainIndex != LastIndex && ChainIndex != Ident::Undefined)
		{
			if(LastIndex != Ident::Undefined - 1)
			{
				Close3DDebugSession();
			}
			Open3DDebugSession(FString::Printf(TEXT("ChainIndex %d"), ChainIndex));
			Color = GetRandomColor(ChainIndex);
			LastIndex = ChainIndex;
		}
		if (ChainIndex != Ident::Undefined)
		{
			ThinZone::DisplayEdgeSegment(EdgeSegment, Color, ChainIndex);
		}
	}
	if (LastIndex != Ident::Undefined - 1)
	{
		Close3DDebugSession();
	}
	Wait();
}

void ThinZone::DisplayThinZoneSides(const TArray<TArray<FEdgeSegment*>>& ThinZoneSides)
{
	F3DDebugSession _(TEXT("ThinZone Sides"));
	int32 ChainIndex = 0;
	EVisuProperty Color;
	for (const TArray<FEdgeSegment*>& ThinZoneSide : ThinZoneSides)
	{
		Color = GetRandomColor(ChainIndex);
		DisplayThinZoneSide(ThinZoneSide, ChainIndex++, Color);
	}
}

void ThinZone::DisplayThinZones(const TArray<FThinZone2D>& ThinZones)
{
	if (ThinZones.Num() > 0)
	{
		F3DDebugSession _(TEXT("Thin Zones"));

		int32 index = 0;
		for (const FThinZone2D& Zone : ThinZones)
		{
			FString Title;
			index++;
			EVisuProperty VisuProperty = EVisuProperty::BlueCurve;
			switch (Zone.GetCategory())
			{
			case EThinZone2DType::Undefined:
				VisuProperty = EVisuProperty::Iso;
				Title = TEXT("Zone UNDEFINED");
				break;
			case EThinZone2DType::Global:
				VisuProperty = EVisuProperty::BluePoint;
				Title = TEXT("Zone GLOBAL");
				break;
			case EThinZone2DType::PeakStart:
				VisuProperty = EVisuProperty::RedPoint;
				Title = TEXT("Zone PEAK start");
				break;
			case EThinZone2DType::PeakEnd:
				VisuProperty = EVisuProperty::OrangePoint;
				Title = TEXT("Zone PEAK end");
				break;
			case EThinZone2DType::Butterfly:
				VisuProperty = EVisuProperty::YellowPoint;
				Title = TEXT("Zone BUTTERFLY");
				break;
			case EThinZone2DType::BetweenLoops:
				VisuProperty = EVisuProperty::PinkPoint;
				Title = TEXT("Zone BETWEEN_CONTOUR");
				break;
			default:
				Title = TEXT("Zone Unknown");
			}

			Zone.Display(Title, VisuProperty);
		}
	}
}

void FThinZone2D::Display(const FString& Title, EVisuProperty VisuProperty) const
{
	F3DDebugSession A(Title);
	ThinZone::DisplayThinZoneSide(GetFirstSide(), 0, VisuProperty);
	ThinZone::DisplayThinZoneSide(GetSecondSide(), 1, VisuProperty);
}

}

#endif
