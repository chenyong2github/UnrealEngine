// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Meshers/IsoTriangulator.h"

#ifdef CADKERNEL_DEV

#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IntersectionSegmentTool.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoCell.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoNode.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoSegment.h"
#include "CADKernel/UI/Display.h"

namespace CADKernel
{

void FIsoTriangulator::DisplayIsoNodes(EGridSpace Space) const
{
	if (!bDisplay)
	{
		return;
	}

	Open3DDebugSession(TEXT("FIsoTrianguler::IsoNodes"));
	for (const FLoopNode& Node : LoopNodes)
	{
		Grid.DisplayIsoNode(Space, Node, Node.GetFaceIndex(), EVisuProperty::YellowPoint);
	}
	Close3DDebugSession();
	Open3DDebugSession(TEXT("FIsoTrianguler::IsoNodes Inner"));
	for (const FIsoNode& Node : InnerNodes)
	{
		Grid.DisplayIsoNode(Space, Node, Node.GetFaceIndex());
	}
	Close3DDebugSession();
}

void FIsoTriangulator::DisplayPixel(const int32 IndexU, const int32 IndexV) const
{
	DisplayPixel(IndexV * Grid.GetCuttingCount(EIso::IsoU) + IndexU);
}

void FIsoTriangulator::DisplayPixel(const int32 Index) const
{
	FPoint Point = (Grid.GetInner2DPoint(EGridSpace::UniformScaled, Index) + Grid.GetInner2DPoint(EGridSpace::UniformScaled, Index + Grid.GetCuttingCount(EIso::IsoU) + 1)) * 0.5;
	DisplayPoint(Point * Grid.DisplayScale, EVisuProperty::GreenPoint);
};

void FIsoTriangulator::DisplayPixels(TArray<uint8>& Pixel) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(TEXT("FIsoTrianguler::Pixel"));
	for (int32 Index = 0; Index <Grid.GetTotalCuttingCount(); ++Index)
	{
		if (Pixel[Index])
		{
			DisplayPixel(Index);
		}
	}
}

void FIsoTriangulator::DisplayLoops(const TCHAR* Message, bool bOneNode, bool bSplitBySegment) const
{
	if (bDisplay)
	{
		F3DDebugSession _(Message);
		{
			{
				F3DDebugSession A(!bOneNode, TEXT("Node Cloud"));
				int32 Index = 0;
				for (const FLoopNode& Node : LoopNodes)
				{
					if (Node.IsDelete())
					{
						Grid.DisplayIsoNode(EGridSpace::UniformScaled, Node, Index++, EVisuProperty::PurplePoint);
					}
					Grid.DisplayIsoNode(EGridSpace::UniformScaled, Node, Index++, EVisuProperty::BluePoint);
				}
			}

			{
				F3DDebugSession C(!bOneNode, TEXT("Segment"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession D(bSplitBySegment, TEXT("Segment"));
					Grid.DisplayIsoSegment(EGridSpace::UniformScaled, Segment->GetFirstNode(), Segment->GetSecondNode(), Index++, EVisuProperty::BlueCurve);
				}
			}

		}
		Wait(false);
	}
}

void FIsoTriangulator::DisplayLoopsByNextAndPrevious(const TCHAR* Message) const
{
	if (bDisplay)
	{
		F3DDebugSession _(Message);
		{
			{
				F3DDebugSession A(TEXT("Node Next"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession B(TEXT("Segment"));
					Grid.DisplayIsoSegment(EGridSpace::UniformScaled, Segment->GetFirstNode(), ((const FLoopNode&)Segment->GetFirstNode()).GetNextNode(), Index++, EVisuProperty::BlueCurve);
				}
			}

			{
				F3DDebugSession C(TEXT("Node Forward"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession B(TEXT("Segment"));
					Grid.DisplayIsoSegment(EGridSpace::UniformScaled, Segment->GetSecondNode(), ((const FLoopNode&)Segment->GetSecondNode()).GetPreviousNode(), Index++, EVisuProperty::BlueCurve);
				}
			}

		}
		Wait(false);
	}
}

void FIsoTriangulator::DisplayCells(const TArray<FCell>& Cells) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(TEXT("Cells"));
	int32 Index = 0;
	for (const FCell& Cell : Cells)
	{
		DisplayCell(Cell);
	}
}

void FIsoTriangulator::DrawCellBoundary(int32 Index, EVisuProperty Property) const
{
	int32 IndexU;
	int32 IndexV;
	Grid.UVIndexFromGlobalIndex(Index, IndexU, IndexV);

	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV) * Grid.DisplayScale, Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1) * Grid.DisplayScale, Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV) * Grid.DisplayScale, Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1) * Grid.DisplayScale, Property, Index);

	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV) * Grid.DisplayScale, Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1) * Grid.DisplayScale, Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV) * Grid.DisplayScale, Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1) * Grid.DisplayScale, Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV) * Grid.DisplayScale, Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV) * Grid.DisplayScale, Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1) * Grid.DisplayScale, Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1) * Grid.DisplayScale, Index, EVisuProperty(Property + 1));
}


void FIsoTriangulator::DisplayCell(const FCell& Cell) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(FString::Printf(TEXT("Cell %d"), Cell.Id));

	DrawCellBoundary(Cell.Id, EVisuProperty::YellowPoint);
	for (const TArray<FLoopNode*>& Nodes : Cell.SubLoops)
	{
		for (const FLoopNode* Node : Nodes)
		{
			Grid.DisplayIsoNode(EGridSpace::UniformScaled, *Node, Cell.Id, EVisuProperty::BluePoint);
		}
	}
}

void FIntersectionSegmentTool::Display(const TCHAR* Message, EVisuProperty Property) const
{
	if (!Grid.bDisplay)
	{
		return;
	}

	int32 Index = 0;

	Open3DDebugSession(Message);
	for (const FSegment4IntersectionTools& Segment : Segments)
	{
		DisplaySegment(Segment.Segment2D[0] * Grid.DisplayScale, Segment.Segment2D[1] * Grid.DisplayScale, Index++, Property);
	}
	Close3DDebugSession();
}


} // namespace CADKernel

#endif
