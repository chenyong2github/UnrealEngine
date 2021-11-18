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

void FIsoTriangulator::Display(EGridSpace Space, const FIsoSegment& Segment, FIdent Ident, EVisuProperty Property, bool bDisplayOrientation) const
{
	DisplaySegment(Segment.GetFirstNode().GetPoint(Space, Grid), Segment.GetSecondNode().GetPoint(Space, Grid), Ident, Property, bDisplayOrientation);
}

void FIsoTriangulator::DisplayTriangle(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, const FIsoNode& NodeC) const
{
	TArray<FPoint> Points;
	Points.SetNum(3);
	Points[0] = NodeA.GetPoint(Space, Grid);
	Points[1] = NodeB.GetPoint(Space, Grid);
	Points[2] = NodeC.GetPoint(Space, Grid);
	DrawElement(2, Points, EVisuProperty::Element);
	DisplaySegment(Points[0], Points[1]); 
	DisplaySegment(Points[1], Points[2]);
	DisplaySegment(Points[2], Points[0]);
}

void FIsoTriangulator::Display(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, FIdent Ident, EVisuProperty Property) const
{
	return DisplaySegment(NodeA.GetPoint(Space, Grid), NodeB.GetPoint(Space, Grid), Ident, Property);
}

void FIsoTriangulator::Display(EGridSpace Space, const FIsoNode& Node, FIdent Ident, EVisuProperty Property) const
{
	return DisplayPoint(Node.GetPoint(Space, Grid), Property, Ident);
}

void FIsoTriangulator::Display(EGridSpace Space, const TCHAR* Message, const TArray<FIsoSegment*>& Segments, bool bDisplayNode, bool bDisplayOrientation, EVisuProperty Property) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(Message);
#ifdef DEBUG_LIMIT
	for (FIsoSegment* Segment : Segments)
	{
		if (Segment->IsALimit())
		{
			Display(Space, *Segment, 0, EVisuProperty::Iso, bDisplayOrientation);
		}
	}
#endif
	for (FIsoSegment* Segment : Segments)
	{
#ifdef DEBUG_LIMIT
		if (!Segment->IsALimit())
#endif
		{
			Display(Space, *Segment, 0, Property, bDisplayOrientation);
		}
	}

	if (bDisplayNode)
	{
		for (FIsoSegment* Segment : Segments)
		{
			Display(Space, Segment->GetFirstNode(), Segment->GetFirstNode().GetFaceIndex());
			Display(Space, Segment->GetSecondNode(), Segment->GetSecondNode().GetFaceIndex());
		}
	}
	//Wait();
}

void FIsoTriangulator::DisplayLoops(EGridSpace Space, const TCHAR* Message, const TArray<FLoopNode>& Nodes, bool bDisplayNode, EVisuProperty Property) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(Message);

	for (const FLoopNode& Node : Nodes)
	{
		{
			Display(Space, Node, Node.GetNextNode(), 0, Property);
		}
	}

	if (bDisplayNode)
	{
		for (const FLoopNode& Node : Nodes)
		{
			Display(Space, Node, Node.GetFaceIndex());
		}
	}
}


void FIsoTriangulator::DisplayIsoNodes(EGridSpace Space) const
{
	if (!bDisplay)
	{
		return;
	}

	Open3DDebugSession(TEXT("FIsoTrianguler::IsoNodes"));
	for (const FLoopNode& Node : LoopNodes)
	{
		Display(Space, Node, Node.GetFaceIndex(), EVisuProperty::YellowPoint);
	}
	Close3DDebugSession();
	Open3DDebugSession(TEXT("FIsoTrianguler::IsoNodes Inner"));
	for (const FIsoNode& Node : InnerNodes)
	{
		Display(Space, Node, Node.GetFaceIndex());
	}
	Close3DDebugSession();
}

void FIsoTriangulator::DisplayPixel(const int32 IndexU, const int32 IndexV) const
{
	DisplayPixel(IndexV * Grid.GetCuttingCount(EIso::IsoU) + IndexU);
}

void FIsoTriangulator::DisplayPixel(const int32 Index) const
{
	FPoint Point = (Grid.GetInner2DPoint(EGridSpace::Default2D, Index) + Grid.GetInner2DPoint(EGridSpace::Default2D, Index + Grid.GetCuttingCount(EIso::IsoU) + 1)) * 0.5;
	DisplayPoint(Point, EVisuProperty::GreenPoint);
};

void FIsoTriangulator::DisplayPixels(TArray<uint8>& Pixel) const
{
	if (!bDisplay)
	{
		return;
	}

	Open3DDebugSession(TEXT("FIsoTrianguler::Pixel"));
	for (int32 Index = 0; Index <Grid.GetTotalCuttingCount(); ++Index)
	{
		if (Pixel[Index])
		{
			DisplayPixel(Index);
		}
	}
	Close3DDebugSession();
}

void FIsoTriangulator::DisplayCycle(const TArray<FIsoSegment*>& Cycle, const TCHAR* Message) const
{
	F3DDebugSession _(Message);
	for (const FIsoSegment* Segment : Cycle)
	{
		Display(EGridSpace::UniformScaled, *Segment);
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

void FIsoTriangulator::DisplayCell(const FCell& Cell) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(FString::Printf(TEXT("Cell %d"), Cell.Id));
	for (const TArray<FLoopNode*>& Nodes : Cell.SubLoops)
	{
		for (const FLoopNode* Node : Nodes)
		{
			DisplayPoint(Node->GetPoint(EGridSpace::UniformScaled, Grid), EVisuProperty::OrangePoint, Cell.Id);
		}
	}
}

void FIntersectionSegmentTool::Display(const TCHAR* Message) const
{
	if (!Grid.bDisplay)
	{
		return;
	}
	
	Open3DDebugSession(Message);
	for (const FSegment4IntersectionTools& Segment : Segments)
	{
		DisplaySegment(Segment.Segment2D[0], Segment.Segment2D[1]);
	}
	Close3DDebugSession();
}


} // namespace CADKernel

#endif
