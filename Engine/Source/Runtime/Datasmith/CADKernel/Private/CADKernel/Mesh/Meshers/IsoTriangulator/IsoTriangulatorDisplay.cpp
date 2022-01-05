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

void FIsoTriangulator::Display(FString Message, EGridSpace Space, const TArray<const FIsoNode*>& Nodes, EVisuProperty Property) const
{
	if (!bDisplay)
	{
		return;
	}

	Open3DDebugSession(Message);
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		Display(Space, *Nodes[Index], Nodes[Index]->GetIndex(), Property);
	}
	Close3DDebugSession();
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
			Display(Space, Segment->GetFirstNode(), Segment->GetFirstNode().GetId(), (EVisuProperty)(Property - 1));
			Display(Space, Segment->GetSecondNode(), Segment->GetSecondNode().GetId(), (EVisuProperty)(Property - 1));
		}
	}
	//Wait();
}

void FIsoTriangulator::DisplayLoops(EGridSpace Space, const TCHAR* Message, const TArray<FLoopNode>& Nodes, bool bDisplayNode, EVisuProperty NodeVisuProperty) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(Message);

	for (const FLoopNode& Node : Nodes)
	{
		if (!Node.IsDelete())
		{
			Display(Space, Node, Node.GetNextNode(), 0, NodeVisuProperty);
		}
	}

	if (bDisplayNode)
	{
		for (const FLoopNode& Node : Nodes)
		{
			if (Node.IsDelete())
			{
				Display(Space, Node, Node.GetFaceIndex(), EVisuProperty::PurplePoint);
			}
			else
			{
				Display(Space, Node, Node.GetFaceIndex(), (EVisuProperty) ((int32)NodeVisuProperty + 1));
			}
		}
	}
}

void FIsoTriangulator::DisplayLoop(EGridSpace Space, const TCHAR* Message, const TArray<FLoopNode*>& Nodes, bool bDisplayNode, EVisuProperty NodeVisuProperty) const
{
	if (!bDisplay)
	{
		return;
	}

	F3DDebugSession _(Message);

	int32 Index = 0;
	for (const FLoopNode* Node : Nodes)
	{
		if (!Node->IsDelete())
		{
			Display(Space, *Node, Node->GetNextNode(), Index++, (EVisuProperty)((int32)NodeVisuProperty + 1));
		}
	}

	if (bDisplayNode)
	{
		for (const FLoopNode* Node : Nodes)
		{
			if (Node->IsDelete())
			{
				Display(Space, *Node, Node->GetFaceIndex(), EVisuProperty::PurplePoint);
			}
			else
			{
				Display(Space, *Node, Node->GetFaceIndex(), (EVisuProperty)((int32)NodeVisuProperty));
			}
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

void FIsoTriangulator::DisplayLoops(const TCHAR* Message, bool bOneNode, bool bSplitBySegment, bool bDisplayNext, bool bDisplayPrevious) const
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
						Display(EGridSpace::UniformScaled, Node, Index++, EVisuProperty::PurplePoint);
					}
					Display(EGridSpace::UniformScaled, Node, Index++, EVisuProperty::BluePoint);
				}
			}

			{
				F3DDebugSession C(!bOneNode, TEXT("Segment"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession D(bSplitBySegment, TEXT("Segment"));
					Display(EGridSpace::UniformScaled, Segment->GetFirstNode(), Segment->GetSecondNode(), Index++, EVisuProperty::BlueCurve);
				}
			}

			if(bDisplayNext)
			{
				F3DDebugSession A(TEXT("Node Next"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession B(bSplitBySegment, TEXT("Segment"));
					Display(EGridSpace::UniformScaled, Segment->GetFirstNode(), ((const FLoopNode&) Segment->GetFirstNode()).GetNextNode(), Index++, EVisuProperty::BlueCurve);
				}
			}

			if(bDisplayPrevious)
			{
				F3DDebugSession C(TEXT("Node Forward"));
				int32 Index = 0;
				for (FIsoSegment* Segment : LoopSegments)
				{
					F3DDebugSession B(bSplitBySegment, TEXT("Segment"));
					Display(EGridSpace::UniformScaled, Segment->GetSecondNode(), ((const FLoopNode&)Segment->GetSecondNode()).GetPreviousNode(), Index++, EVisuProperty::BlueCurve);
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

	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV), Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1), Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV), Property, Index);
	DisplayPoint(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1), Property, Index);

	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV), Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1), Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV), Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1), Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV), Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV), Index, EVisuProperty(Property + 1));
	DisplaySegment(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1), Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV + 1), Index, EVisuProperty(Property + 1));
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
			DisplayPoint(Node->GetPoint(EGridSpace::UniformScaled, Grid), EVisuProperty::BluePoint, Cell.Id);
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
		DisplaySegment(Segment.Segment2D[0], Segment.Segment2D[1], Index++, Property);
	}
	Close3DDebugSession();
}


} // namespace CADKernel

#endif
