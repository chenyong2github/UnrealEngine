// Copyright Epic Games, Inc. All Rights Reserved.


#include "ContourList.h"
#include "Data.h"
#include "Part.h"
#include "Intersection.h"
#include "GlyphLoader.h"

#include "Math/UnrealMathUtility.h"


FContourList::FContourList(const FT_GlyphSlot Glyph, const TSharedPtr<FData> DataIn)
	: Data(DataIn)
{
	FGlyphLoader().Load(Glyph, Data, this);
	Init();
}

FContour& FContourList::Add()
{
	AddTail(FContour(Data));
	return GetTail()->GetValue();
}

void FContourList::Remove(const FContour& Contour)
{
	// Search with comparing pointers
	for (TDoubleLinkedList<FContour>::TDoubleLinkedListNode* Node = GetHead(); Node; Node = Node->GetNextNode())
	{
		if (&Node->GetValue() == &Contour)
		{
			RemoveNode(Node);
			break;
		}
	}
}

void FContourList::Reset()
{
	for (FContour& Contour : *this)
	{
		for (FPart* const Part : Contour)
		{
			Part->ResetDoneExpand();
			Part->ResetInitialPosition();
		}

		Contour.ResetContour();
	}
}

void FContourList::Init()
{
	for (FContour& Contour : *this)
	{
		for (FPart* const Point : Contour)
		{
			if (!Point->bSmooth)
			{
				Point->ComputeSmooth();
			}

			Point->ResetInitialPosition();
		}

		for (FPart* const Point : Contour)
		{
			Contour.ComputeAvailableExpandNear(Point);
		}

		if (Contour.Num() > FIntersection::MinContourSizeForIntersectionFar)
		{
			for (FPart* const Point : Contour)
			{
				Contour.ComputeAvailableExpandsFarFrom(Point);
			}
		}
	}
}
