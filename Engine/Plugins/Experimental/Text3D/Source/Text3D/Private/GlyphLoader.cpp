// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlyphLoader.h"
#include "Contour.h"
#include "Part.h"
#include "ContourList.h"

FGlyphLoader::FGlyphLoader()
	: EndIndex(-1)
{

}

struct FGlyphLoader::FContourNode
{
	FContourNode(FContour* const ContourIn)
		: Contour(ContourIn)
	{

	}


	FContour* Contour;
	TArray<FContourNode*> Nodes;
};

void FGlyphLoader::Load(const FT_GlyphSlot Glyph, const TSharedPtr<FData> Data, FContourList* Contours)
{
	const FT_Outline Outline = Glyph->outline;
	const int32 ContourCount = Outline.n_contours;
	Clockwise.Reserve(ContourCount);
	FContourNode Root(nullptr);

	for (int32 Index = 0; Index < ContourCount; Index++)
	{
		if (CreateContour(Outline, Index, Contours))
		{
			ComputeInitialParity();
			Insert(new FContourNode(Contour), &Root);
		}
	}

	FixParity(&Root, false);
}


FGlyphLoader::FLine::FLine(const FVector2D PositionIn)
	: Position(PositionIn)
{

}

void FGlyphLoader::FLine::Add(FGlyphLoader* const Loader)
{
	if (!Loader->Contour->Num() || !(Position - Loader->FirstPosition).IsNearlyZero())
	{
		FPart* const Point = Loader->AddPoint(Position);
		Loader->JoinWithLast(Point);
		Loader->LastPoint = Point;
	}
}


FGlyphLoader::FCurve::FCurve(const bool bLineIn)
	: bLine(bLineIn)

	, StartT(0.f)
	, EndT(1.f)
{

}

FGlyphLoader::FCurve::~FCurve()
{

}

struct FGlyphLoader::FCurve::FPointData
{
	float T;
	FVector2D Position;
	FVector2D Tangent;
	FPart* Point;
};

void FGlyphLoader::FCurve::Add(FGlyphLoader* const LoaderIn)
{
	Loader = LoaderIn;

	if (bLine)
	{
		FLine(Position(StartT)).Add(Loader);
		return;
	}

	Depth = 0;


	FPointData Start;
	Start.T = StartT;
	Start.Position = Position(Start.T);
	Start.Tangent = Tangent(Start.T);
	Start.Point = Loader->AddPoint(Start.Position);
	First = Start.Point;
	bFirstSplit = false;

	FPointData End;
	End.T = EndT;
	End.Position = Position(End.T);
	End.Tangent = Tangent(End.T);
	End.Point = nullptr;
	Last = End.Point;
	bLastSplit = false;


	Loader->JoinWithLast(Start.Point);
	ComputeMaxDepth();
	Split(Start, End);
}

bool FGlyphLoader::FCurve::OnOneLine(const FVector2D A, const FVector2D B, const FVector2D C)
{
	return FMath::IsNearlyZero(FVector2D::CrossProduct((B - A).GetSafeNormal(), (C - A).GetSafeNormal()));
}

void FGlyphLoader::FCurve::ComputeMaxDepth()
{
	// Compute approximate curve length with 4 points
	const float MinStep = 30.f;
	const float StepT = 0.333f;
	float Length = 0.f;

	FVector2D Prev;
	FVector2D Curr = Position(StartT);

	for (float T = StartT + StepT; T < EndT; T += StepT)
	{
		Prev = Curr;
		Curr = Position(T);

		Length += (Curr - Prev).Size();
	}

	const float MaxStepCount = Length / MinStep;
	MaxDepth = static_cast<int32>(FMath::Log2(MaxStepCount)) + 1;
}

void FGlyphLoader::FCurve::Split(const FPointData& Start, const FPointData& End)
{
	Depth++;
	FPointData Middle;

	Middle.T = (Start.T + End.T) / 2.f;
	Middle.Position = Position(Middle.T);
	Middle.Tangent = Tangent(Middle.T);
	Middle.Point = Loader->AddPoint(Middle.Position);
	Middle.Point->bSmooth = true;


	Start.Point->Next = Middle.Point;
	Middle.Point->Prev = Start.Point;
	Middle.Point->Next = End.Point;

	if (End.Point)
	{
		End.Point->Prev = Middle.Point;
	}
	else
	{
		Loader->LastPoint = Middle.Point;
	}


	CheckPart(Start, Middle);
	UpdateTangent(&Middle);
	CheckPart(Middle, End);
	Depth--;
}

void FGlyphLoader::FCurve::CheckPart(const FPointData& Start, const FPointData& End)
{
	const FVector2D Side = (End.Position - Start.Position).GetSafeNormal();

	if ((FVector2D::DotProduct(Side, Start.Tangent) > FPart::CosMaxAngleSideTangent && FVector2D::DotProduct(Side, End.Tangent) > FPart::CosMaxAngleSideTangent) || Depth >= MaxDepth)
	{
		if (!bFirstSplit && Start.Point == First)
		{
			bFirstSplit = true;
			Split(Start, End);
		}
		else if (!bLastSplit && End.Point == Last)
		{
			bLastSplit = true;
			Split(Start, End);
		}
		else
		{
			Start.Point->TangentX = Side;
		}
	}
	else
	{
		Split(Start, End);
	}
}

void FGlyphLoader::FCurve::UpdateTangent(FPointData* const Middle)
{

}


FGlyphLoader::FQuadraticCurve::FQuadraticCurve(const FVector2D A, const FVector2D B, const FVector2D C)
	: FCurve(OnOneLine(A, B, C))

	, E(A - 2 * B + C)
	, F(-A + B)
	, G(A)
{

}

FVector2D FGlyphLoader::FQuadraticCurve::Position(const float T) const
{
	return E * T * T + 2 * F * T + G;
}

FVector2D QuadraticCurveTangent(const FVector2D E, const FVector2D F, const float T)
{
	const FVector2D Result = E * T + F;

	if (Result.IsNearlyZero())
	{
		// Just some vector with non-zero length
		return {1.f, 0.f};
	}

	return Result;
}

FVector2D FGlyphLoader::FQuadraticCurve::Tangent(const float T)
{
	return QuadraticCurveTangent(E, F, T).GetSafeNormal();
}


FGlyphLoader::FCubicCurve::FCubicCurve(const FVector2D A, const FVector2D B, const FVector2D C, const FVector2D D)
	: FCurve(OnOneLine(A, B, C) && OnOneLine(B, C, D))

	, E(-A + 3 * B - 3 * C + D)
	, F(A - 2 * B + C)
	, G(-A + B)
	, H(A)
{
	if (!bLine)
	{
		bSharpStart = G.IsNearlyZero();
		bSharpEnd = (C - D).IsNearlyZero();
	}
}

void FGlyphLoader::FCubicCurve::UpdateTangent(FPointData* const Middle)
{
	// In this point curve is not smooth, and  r'(t + 0) / |r'(t + 0)| = -r'(t - 0) / |r'(t - 0)|
	if (bSharpMiddle)
	{
		bSharpMiddle = false;
		Middle->Tangent *= -1;
		Middle->Point->bSmooth = false;
	}
}

FVector2D FGlyphLoader::FCubicCurve::Position(const float T) const
{
	return E * T * T * T + 3 * F * T * T + 3 * G * T + H;
}

FVector2D FGlyphLoader::FCubicCurve::Tangent(const float T)
{
	FVector2D Result;

	// Using  r' / |r'|  for sharp start and end
	if (bSharpStart && FMath::IsNearlyEqual(T, StartT))
	{
		Result = F;
	}
	else if (bSharpEnd && FMath::IsNearlyEqual(T, EndT))
	{
		Result = -(E + F);
	}
	else
	{
		Result = E * T * T + 2 * F * T + G;
		bSharpMiddle = Result.IsNearlyZero();

		if (bSharpMiddle)
		{
			// Using derivative of quadratic bezier curve (A, B, C) in this point
			Result = QuadraticCurveTangent(F, G, T);
		}
	}

	return Result.GetSafeNormal();
}


bool FGlyphLoader::CreateContour(const FT_Outline Outline, const int32 ContourIndex, FContourList* Contours)
{
	const int32 StartIndex = EndIndex + 1;
	EndIndex = Outline.contours[ContourIndex];
	const int32 ContourLength = EndIndex - StartIndex + 1;

	if (ContourLength < 3)
	{
		return false;
	}

	Contour = &Contours->Add();


	const FT_Vector* const Points = Outline.points + StartIndex;
	auto ToFVector2D = [Points](const int32 Index)
	{
		const FT_Vector Point = Points[Index];
		return FVector2D(Point.x, Point.y);
	};

	FVector2D Prev;
	FVector2D Curr = ToFVector2D(ContourLength - 1);
	FVector2D Next = ToFVector2D(0);
	FVector2D NextNext = ToFVector2D(1);


	const char* const Tags = Outline.tags + StartIndex;
	auto Tag = [Tags](int32 Index)
	{
		return FT_CURVE_TAG(Tags[Index]);
	};

	int32 TagPrev;
	int32 TagCurr = Tag(ContourLength - 1);
	int32 TagNext = Tag(0);


	FVector2D& FirstPositionLocal = FirstPosition;
	FPart* const& LastPointLocal = LastPoint;
	FContour*& ContourLocal = Contour;
	auto ContourIsBad = [&FirstPositionLocal, &LastPointLocal, &ContourLocal](const FVector2D Point)
	{
		if (ContourLocal->Num() == 0)
		{
			FirstPositionLocal = Point;
			return false;
		}

		return (Point - LastPointLocal->Position).IsNearlyZero();
	};

	auto RemoveContour = [Contours, &ContourLocal]()
	{
		Contours->Remove(*ContourLocal);
		ContourLocal = nullptr;
	};


	for (int32 Index = 0; Index < ContourLength; Index++)
	{
		const int32 NextIndex = (Index + 1) % ContourLength;

		Prev = Curr;
		Curr = Next;
		Next = NextNext;
		NextNext = ToFVector2D((NextIndex + 1) % ContourLength);

		TagPrev = TagCurr;
		TagCurr = TagNext;
		TagNext = Tag(NextIndex);

		if (TagCurr == FT_Curve_Tag_On)
		{
			if (TagNext == FT_Curve_Tag_Cubic || TagNext == FT_Curve_Tag_Conic)
			{
				continue;
			}

			if (ContourIsBad(Curr))
			{
				RemoveContour();
				return false;
			}

			if (TagNext == FT_Curve_Tag_On && (Curr - Next).IsNearlyZero())
			{
				continue;
			}

			FLine(Curr).Add(this);
		}
		else if (TagCurr == FT_Curve_Tag_Conic)
		{
			FVector2D A;

			if (TagPrev == FT_Curve_Tag_On)
			{
				if (ContourIsBad(Prev))
				{
					RemoveContour();
					return false;
				}

				A = Prev;
			}
			else
			{
				A = (Prev + Curr) / 2.f;
			}

			FQuadraticCurve(A, Curr, TagNext == FT_Curve_Tag_Conic ? (Curr + Next) / 2.f : Next).Add(this);
		}
		else if (TagCurr == FT_Curve_Tag_Cubic && TagNext == FT_Curve_Tag_Cubic)
		{
			if (ContourIsBad(Prev))
			{
				RemoveContour();
				return false;
			}

			FCubicCurve(Prev, Curr, Next, NextNext).Add(this);
		}
	}

	if (Contour->Num() < 3)
	{
		RemoveContour();
		return false;
	}

	JoinWithLast((*Contour)[0]);

	for (FPart* const Edge : *Contour)
	{
		if (!Edge->ComputeNormal())
		{
			RemoveContour();
			return false;
		}
	}

	return true;
}

void FGlyphLoader::ComputeInitialParity()
{
	float DoubledArea = 0.0f;

	FVector2D Curr;
	FVector2D Next = FirstPosition;

	Next = (*Contour)[1]->Position - FirstPosition;

	for (FPart* Point = (*Contour)[2]; Point != (*Contour)[0]; Point = Point->Next)
	{
		Curr = Next;
		Next = Point->Position - FirstPosition;

		DoubledArea += Curr ^ Next;
	}

	Clockwise.Add(TPair<const FContour*, bool>(Contour, DoubledArea < 0));
}

void FGlyphLoader::Insert(FContourNode* const NodeA, FContourNode* const NodeB)
{
	TArray<FContourNode*>& BNodes = NodeB->Nodes;
	const FContour* ContourA = NodeA->Contour;

	for (int32 IndexC = 0; IndexC < BNodes.Num(); IndexC++)
	{
		FContourNode*& NodeC = BNodes[IndexC];
		const FContour* ContourC = NodeC->Contour;

		if (Inside(ContourA, ContourC))
		{
			Insert(NodeA, NodeC);
			return;
		}

		if (Inside(ContourC, ContourA))
		{
			// add contourC to list of contours that are inside contourA
			TArray<FContourNode*>& ANodes = NodeA->Nodes;
			ANodes.Add(NodeC);
			// replace contourC with contourA in list it was before
			NodeC = NodeA;

			// check if other contours in that list are inside contourA

			for (int32 Index = BNodes.Num() - 1; Index > IndexC; Index--)
			{
				FContourNode* Node = BNodes[Index];

				if (Inside(Node->Contour, ContourA))
				{
					ANodes.Add(Node);
					BNodes.RemoveAt(Index);
				}
			}

			return;
		}
	}

	BNodes.Add(NodeA);
}

void FGlyphLoader::FixParity(FContourNode* const Node, const bool bClockwiseIn)
{
	for (FContourNode* NodeA : Node->Nodes)
	{
		FixParity(NodeA, !bClockwiseIn);
		FContour* const ContourB = NodeA->Contour;
		delete NodeA;

		if (bClockwiseIn != Clockwise[ContourB])
		{
			for (FPart* const Point : *ContourB)
			{
				Swap(Point->Prev, Point->Next);
			}


			FPart* const First = (*ContourB)[0];
			FPart* const Last = First->Prev;

			const FVector2D TangentFirst = First->TangentX;

			for (FPart* Edge = First; Edge != Last; Edge = Edge->Next)
			{
				Edge->TangentX = -Edge->Next->TangentX;
			}

			Last->TangentX = -TangentFirst;


			for (FPart* const Point : *ContourB)
			{
				Point->Normal *= -1.0f;
			}
		}
	}
}

FPart* FGlyphLoader::AddPoint(const FVector2D Position)
{
	FPart* const Point = new FPart();
	Contour->Add(Point);
	Point->Position = Position;
	return Point;
}

void FGlyphLoader::JoinWithLast(FPart* const Point)
{
	if (Contour->Num() > 1)
	{
		LastPoint->Next = Point;
		Point->Prev = LastPoint;

		LastPoint->ComputeTangentX();
	}
}

bool FGlyphLoader::Inside(const FContour* const ContourA, const FContour* const ContourB)
{
	const int32 BPointCount = ContourB->Num();
	// compute angle to which vector "b(i) - a(0)" is rotated when "i" iterates over all points of contourB
	float AngleTotal = 0;

	float AnglePrev;
	float AngleCurr;

	const TMap<const FContour*, bool>& ClockwiseLocal = Clockwise;
	auto ComputeAngleCurr = [ContourB, &ClockwiseLocal, BPointCount, ContourA, &AngleCurr](const int32 EndPoint)
	{
		// use counterclockwise version of contour if it's clockwise
		const FVector2D Delta = (*ContourB)[ClockwiseLocal[ContourB] ? BPointCount - 1 - EndPoint : EndPoint]->Position - (*ContourA)[0]->Position;
		AngleCurr = FMath::Atan2(Delta.Y, Delta.X);
	};

	ComputeAngleCurr(0);

	for (int32 Index = 0; Index < BPointCount; Index++)
	{
		AnglePrev = AngleCurr;
		ComputeAngleCurr((Index + 1) % BPointCount);

		float DeltaAngle = AngleCurr - AnglePrev;

		if (DeltaAngle < -PI)
		{
			DeltaAngle += 2 * PI;
		}

		if (DeltaAngle > PI)
		{
			DeltaAngle -= 2 * PI;
		}

		AngleTotal += DeltaAngle;
	}

	// if contourA is inside contourB, angle is 2pi, else it's 0
	return AngleTotal > 3;
}
