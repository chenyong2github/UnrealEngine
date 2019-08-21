// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Input/HittestGrid.h"
#include "Rendering/RenderingCommon.h"
#include "SlateGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogHittestDebug, Display, All);

DECLARE_CYCLE_STAT(TEXT("HitTestGrid AddWidget"), STAT_SlateHTG_AddWidget, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("HitTestGrid RemoveWidget"), STAT_SlateHTG_RemoveWidget, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("HitTestGrid Clear"), STAT_SlateHTG_Clear, STATGROUP_Slate);

int32 SlateVerifyHitTestVisibility = 0;
static FAutoConsoleVariableRef CVarSlateVerifyHitTestVisibility(TEXT("Slate.VerifyHitTestVisibility"), SlateVerifyHitTestVisibility, TEXT("Should we double check the visibility of widgets during hit testing, in case previously resolved hit tests that same frame may have changed state?"), ECVF_Default);

//
// Helper Functions
//

FVector2D ClosestPointOnSlateRotatedRect(const FVector2D &Point, const FSlateRotatedRect& RotatedRect)
{
	//no need to do any testing if we are inside of the rect
	if (RotatedRect.IsUnderLocation(Point))
	{
		return Point;
	}

	const static int32 NumOfCorners = 4;
	FVector2D Corners[NumOfCorners];
	Corners[0] = RotatedRect.TopLeft;
	Corners[1] = Corners[0] + RotatedRect.ExtentX;
	Corners[2] = Corners[1] + RotatedRect.ExtentY;
	Corners[3] = Corners[0] + RotatedRect.ExtentY;

	FVector2D RetPoint;
	float ClosestDistSq = FLT_MAX;
	for (int32 i = 0; i < NumOfCorners; ++i)
	{
		//grab the closest point along the line segment
		const FVector2D ClosestPoint = FMath::ClosestPointOnSegment2D(Point, Corners[i], Corners[(i + 1) % NumOfCorners]);

		//get the distance between the two
		const float TestDist = FVector2D::DistSquared(Point, ClosestPoint);

		//if the distance is smaller than the current smallest, update our closest
		if (TestDist < ClosestDistSq)
		{
			RetPoint = ClosestPoint;
			ClosestDistSq = TestDist;
		}
	}

	return RetPoint;
}

FORCEINLINE float DistanceSqToSlateRotatedRect(const FVector2D &Point, const FSlateRotatedRect& RotatedRect)
{
	return FVector2D::DistSquared(ClosestPointOnSlateRotatedRect(Point, RotatedRect), Point);
}

FORCEINLINE bool IsOverlappingSlateRotatedRect(const FVector2D& Point, const float Radius, const FSlateRotatedRect& RotatedRect)
{
	return DistanceSqToSlateRotatedRect( Point, RotatedRect ) <= (Radius * Radius);
}

bool ContainsInteractableWidget(const TArray<FWidgetAndPointer>& PathToTest)
{
	for (int32 i = PathToTest.Num() - 1; i >= 0; --i)
	{
		const FWidgetAndPointer& WidgetAndPointer = PathToTest[i];
		if (WidgetAndPointer.Widget->IsInteractable())
		{
			return true;
		}
	}
	return false;
}


const FVector2D CellSize(128.0f, 128.0f);

//
// FHittestGrid::FGridTestingParams
//
struct FHittestGrid::FGridTestingParams
{
	/** Ctor */
	FGridTestingParams()
	: CellCoord(-1, -1)
	, CursorPositionInGrid(FVector2D::ZeroVector)
	, Radius(-1.0f)
	, bTestWidgetIsInteractive(false)
	{}

	FIntPoint CellCoord;
	FVector2D CursorPositionInGrid;
	float Radius;
	bool bTestWidgetIsInteractive;
};

//
// FHittestGrid::FCell
//

FHittestGrid::FCell::FCell()
	: bRequiresSort(false)
{

}

void FHittestGrid::FCell::AddIndex(int32 WidgetIndex)
{
	CachedWidgetIndexes.Add(WidgetIndex);
	bRequiresSort = true;
}

void FHittestGrid::FCell::RemoveIndex(int32 WidgetIndex)
{
	for (int32 CallArrayIndex = 0; CallArrayIndex < CachedWidgetIndexes.Num(); CallArrayIndex++)
	{
		if (CachedWidgetIndexes[CallArrayIndex] == WidgetIndex)
		{
			CachedWidgetIndexes.RemoveAt(CallArrayIndex);
			break;
		}
	}
}

void FHittestGrid::FCell::Sort(const TSparseArray<FWidgetData>& InWidgetArray)
{
	if (bRequiresSort)
	{
		CachedWidgetIndexes.StableSort([&InWidgetArray](const int32& A, const int32& B)
		{
			return InWidgetArray[A].PrimarySort < InWidgetArray[B].PrimarySort || (InWidgetArray[A].PrimarySort == InWidgetArray[B].PrimarySort && InWidgetArray[A].SecondarySort < InWidgetArray[B].SecondarySort);
		});

		bRequiresSort = false;
	}
}

const TArray<int32>& FHittestGrid::FCell::GetCachedWidgetIndexes() const
{
	return CachedWidgetIndexes;
}

//
// FHittestGrid
//

FHittestGrid::FHittestGrid()
	: WidgetMap()
	, WidgetArray()
	, Cells()
	, NumCells(0, 0)
	, GridOrigin(0, 0)
	, GridSize(0, 0)
{
}

FHittestGrid::~FHittestGrid()
{
}

TArray<FWidgetAndPointer> FHittestGrid::GetBubblePath(FVector2D DesktopSpaceCoordinate, float CursorRadius, bool bIgnoreEnabledStatus)
{
	checkSlow(IsInGameThread());

	const FVector2D CursorPositionInGrid = DesktopSpaceCoordinate - GridOrigin;

	if (WidgetArray.Num() > 0 && Cells.Num() > 0)
	{
		TArray<FIndexAndDistance, TInlineAllocator<9>> Hits;

		FGridTestingParams TestingParams;
		TestingParams.CursorPositionInGrid = CursorPositionInGrid;
		TestingParams.CellCoord = GetCellCoordinate(CursorPositionInGrid);
		TestingParams.Radius = 0.0f;
		TestingParams.bTestWidgetIsInteractive = false;

		// First add the exact point test results
		Hits.Add(GetHitIndexFromCellIndex(TestingParams));

		// If we have a cursor radius greater than a pixel in size add any extra hit possibilities.
		if (false) //CursorRadius >= 0.5f)
		{
			TestingParams.Radius = CursorRadius;
			TestingParams.bTestWidgetIsInteractive = true;

			const FVector2D RadiusVector(CursorRadius, CursorRadius);
			const FIntPoint ULIndex = GetCellCoordinate(CursorPositionInGrid - RadiusVector);
			const FIntPoint LRIndex = GetCellCoordinate(CursorPositionInGrid + RadiusVector);

			for (int32 YIndex = ULIndex.Y; YIndex <= LRIndex.Y; ++YIndex)
			{
				for (int32 XIndex = ULIndex.X; XIndex <= LRIndex.X; ++XIndex)
				{
					const FIntPoint PointToTest(XIndex, YIndex);
					if (IsValidCellCoord(PointToTest))
					{
						TestingParams.CellCoord = PointToTest;
						Hits.Add(GetHitIndexFromCellIndex(TestingParams));
					}
				}
			}

			// sort the paths, and get the closest one that is valid
			Hits.StableSort([](const FIndexAndDistance& A, const FIndexAndDistance& B)
			{
				return A.WidgetIndex != INDEX_NONE && A.DistanceSqToWidget < B.DistanceSqToWidget;
			});
		}

		const FIndexAndDistance& BestHit = Hits[0];
		if (BestHit.WidgetIndex != INDEX_NONE)
		{
			const FWidgetData& BestHitWidgetData = WidgetArray[BestHit.WidgetIndex];

			const TSharedPtr<SWidget> FirstHitWidget = BestHitWidgetData.GetWidget();
			if (FirstHitWidget.IsValid()) // Make Sure we landed on a valid widget
			{
				TArray<FWidgetAndPointer> Path;

				TSharedPtr<SWidget> CurWidget = FirstHitWidget;
				while (CurWidget.IsValid())
				{
					FGeometry DesktopSpaceGeometry = CurWidget->GetPaintSpaceGeometry();
					DesktopSpaceGeometry.AppendTransform(FSlateLayoutTransform(GridOrigin - GridWindowOrigin));

					Path.Emplace(FArrangedWidget(CurWidget.ToSharedRef(), DesktopSpaceGeometry), TSharedPtr<FVirtualPointerPosition>());
					CurWidget = CurWidget->Advanced_GetPaintParentWidget();
				}

				if (!Path.Last().Widget->Advanced_IsWindow())
				{
					return TArray<FWidgetAndPointer>();
				}

				Algo::Reverse(Path);

				bool bRemovedDisabledWidgets = false;
				if (!bIgnoreEnabledStatus)
				{
					// @todo It might be more correct to remove all disabled widgets and non-hit testable widgets.  It doesn't make sense to have a hit test invisible widget as a leaf in the path
					// and that can happen if we remove a disabled widget. Furthermore if we did this we could then append custom paths in all cases since the leaf most widget would be hit testable
					// For backwards compatibility changing this could be risky
					const int32 DisabledWidgetIndex = Path.IndexOfByPredicate([](const FArrangedWidget& SomeWidget) { return !SomeWidget.Widget->IsEnabled(); });
					if (DisabledWidgetIndex != INDEX_NONE)
					{
						bRemovedDisabledWidgets = true;
						Path.RemoveAt(DisabledWidgetIndex, Path.Num() - DisabledWidgetIndex);
					}
				}

				if (!bRemovedDisabledWidgets && Path.Num() > 0)
				{
					if (BestHitWidgetData.CustomPath.IsValid())
					{
						const TArray<FWidgetAndPointer> BubblePathExtension = BestHitWidgetData.CustomPath.Pin()->GetBubblePathAndVirtualCursors(FirstHitWidget->GetTickSpaceGeometry(), DesktopSpaceCoordinate, bIgnoreEnabledStatus);
						Path.Append(BubblePathExtension);
					}
				}
	
				return Path;
			}
		}
	}

	return TArray<FWidgetAndPointer>();
}

bool FHittestGrid::SetHittestArea(const FVector2D& HittestPositionInDesktop, const FVector2D& HittestDimensions, const FVector2D& HitestOffsetInWindow)
{
	bool bWasCleared = false;

	// If the size of the hit test area changes we need to clear it out
	if (GridSize != HittestDimensions)
	{
		GridSize = HittestDimensions;
		NumCells = FIntPoint(FMath::CeilToInt(GridSize.X / CellSize.X), FMath::CeilToInt(GridSize.Y / CellSize.Y));
		
		const int32 NewTotalCells = NumCells.X * NumCells.Y;
		Cells.Reset(NewTotalCells);
		Cells.SetNum(NewTotalCells);

		WidgetMap.Reset();
		WidgetArray.Reset();

		bWasCleared = true;
	}

	GridOrigin = HittestPositionInDesktop;
	GridWindowOrigin = HitestOffsetInWindow;

	return bWasCleared;
}

void FHittestGrid::Clear()
{
	SCOPE_CYCLE_COUNTER(STAT_SlateHTG_Clear);
	const int32 TotalCells = Cells.Num();
	Cells.Reset(TotalCells);
	Cells.SetNum(TotalCells);

	WidgetMap.Reset();
	WidgetArray.Reset();
}

bool FHittestGrid::IsDescendantOf(const TSharedRef<SWidget> Parent, const FWidgetData& ChildData)
{
	const TSharedPtr<SWidget> ChildWidgetPtr = ChildData.GetWidget();
	if (ChildWidgetPtr == Parent)
	{
		return false;
	}

	const SWidget* ParentWidget = &Parent.Get();
	SWidget* CurWidget = ChildWidgetPtr.Get();

	while (CurWidget)
	{
		if (ParentWidget == CurWidget)
		{
			return true;
		}
		CurWidget = CurWidget->Advanced_GetPaintParentWidget().Get();
	}

	return false;
}

template<typename TCompareFunc, typename TSourceSideFunc, typename TDestSideFunc>
TSharedPtr<SWidget> FHittestGrid::FindFocusableWidget(FSlateRect WidgetRect, const FSlateRect SweptRect, int32 AxisIndex, int32 Increment, const EUINavigation Direction, const FNavigationReply& NavigationReply, TCompareFunc CompareFunc, TSourceSideFunc SourceSideFunc, TDestSideFunc DestSideFunc)
{
	FIntPoint CurrentCellPoint = GetCellCoordinate(WidgetRect.GetCenter());

	int32 StartingIndex = CurrentCellPoint[AxisIndex];

	float CurrentSourceSide = SourceSideFunc(WidgetRect);

	int32 StrideAxis, StrideAxisMin, StrideAxisMax;

	// Ensure that the hit test grid is valid before proceeding
	if (NumCells.X < 1 || NumCells.Y < 1)
	{
		return TSharedPtr<SWidget>();
	}

	if (AxisIndex == 0)
	{
		StrideAxis = 1;
		StrideAxisMin = FMath::Min(FMath::Max(FMath::FloorToInt(SweptRect.Top / CellSize.Y), 0), NumCells.Y - 1);
		StrideAxisMax = FMath::Min(FMath::Max(FMath::FloorToInt(SweptRect.Bottom / CellSize.Y), 0), NumCells.Y - 1);
	}
	else
	{
		StrideAxis = 0;
		StrideAxisMin = FMath::Min(FMath::Max(FMath::FloorToInt(SweptRect.Left / CellSize.X), 0), NumCells.X - 1);
		StrideAxisMax = FMath::Min(FMath::Max(FMath::FloorToInt(SweptRect.Right / CellSize.X), 0), NumCells.X - 1);
	}

	bool bWrapped = false;
	while (CurrentCellPoint[AxisIndex] >= 0 && CurrentCellPoint[AxisIndex] < NumCells[AxisIndex])
	{
		FIntPoint StrideCellPoint = CurrentCellPoint;
		int32 CurrentCellProcessed = CurrentCellPoint[AxisIndex];

		// Increment before the search as a wrap case will change our current cell.
		CurrentCellPoint[AxisIndex] += Increment;

		FSlateRect BestWidgetRect;
		TSharedPtr<SWidget> BestWidget = TSharedPtr<SWidget>();

		for (StrideCellPoint[StrideAxis] = StrideAxisMin; StrideCellPoint[StrideAxis] <= StrideAxisMax; ++StrideCellPoint[StrideAxis])
		{
			const FHittestGrid::FCell& Cell = FHittestGrid::CellAt(StrideCellPoint.X, StrideCellPoint.Y);
			const TArray<int32>& IndexesInCell = Cell.GetCachedWidgetIndexes();

			for (int32 i = IndexesInCell.Num() - 1; i >= 0; --i)
			{
				const int32 CurrentIndex = IndexesInCell[i];
				checkSlow(WidgetArray.IsValidIndex(CurrentIndex));

				const FWidgetData& TestCandidate = WidgetArray[CurrentIndex];
				const TSharedPtr<SWidget> TestWidget = TestCandidate.GetWidget();
				if (TestWidget.IsValid())
				{
					FGeometry TestCandidateGeo = TestWidget->GetPaintSpaceGeometry();
					TestCandidateGeo.AppendTransform(FSlateLayoutTransform(-GridWindowOrigin));
					const FSlateRect TestCandidateRect = TestCandidateGeo.GetRenderBoundingRect();

					if (CompareFunc(DestSideFunc(TestCandidateRect), CurrentSourceSide) && FSlateRect::DoRectanglesIntersect(SweptRect, TestCandidateRect))
					{
						// If this found widget isn't closer then the previously found widget then keep looking.
						if (BestWidget.IsValid() && !CompareFunc(DestSideFunc(BestWidgetRect), DestSideFunc(TestCandidateRect)))
						{
							continue;
						}

						// If we have a non escape boundary condition and this widget isn't a descendant of our boundary condition widget then it's invalid so we keep looking.
						if (NavigationReply.GetBoundaryRule() != EUINavigationRule::Escape
							&& NavigationReply.GetHandler().IsValid()
							&& !IsDescendantOf(NavigationReply.GetHandler().ToSharedRef(), TestCandidate))
						{
							continue;
						}

						if (TestWidget->IsEnabled() && TestWidget->SupportsKeyboardFocus())
						{
							BestWidgetRect = TestCandidateRect;
							BestWidget = TestWidget;
						}
					}
				}
			}
		}

		if (BestWidget.IsValid())
		{
			// Check for the need to apply our rule
			if (CompareFunc(DestSideFunc(BestWidgetRect), SourceSideFunc(SweptRect)))
			{
				switch (NavigationReply.GetBoundaryRule())
				{
				case EUINavigationRule::Explicit:
					return NavigationReply.GetFocusRecipient();
				case EUINavigationRule::Custom:
				case EUINavigationRule::CustomBoundary:
				{
					const FNavigationDelegate& FocusDelegate = NavigationReply.GetFocusDelegate();
					if (FocusDelegate.IsBound())
					{
						return FocusDelegate.Execute(Direction);
					}
					return TSharedPtr<SWidget>();
				}
				case EUINavigationRule::Stop:
					return TSharedPtr<SWidget>();
				case EUINavigationRule::Wrap:
					CurrentSourceSide = DestSideFunc(SweptRect);
					FVector2D SampleSpot = WidgetRect.GetCenter();
					SampleSpot[AxisIndex] = CurrentSourceSide;
					CurrentCellPoint = GetCellCoordinate(SampleSpot);
					bWrapped = true;
					break;
				}
			}

			return BestWidget;
		}

		// break if we have looped back to where we started.
		if (bWrapped && StartingIndex == CurrentCellProcessed) { break; }

		// If were going to fail our bounds check and our rule is to a boundary condition (Wrap or CustomBoundary) handle appropriately
		if (!(CurrentCellPoint[AxisIndex] >= 0 && CurrentCellPoint[AxisIndex] < NumCells[AxisIndex]))
		{
			if (NavigationReply.GetBoundaryRule() == EUINavigationRule::Wrap)
			{
				if (bWrapped)
				{
					// If we've already wrapped, unfortunately it must be that the starting widget wasn't within the boundary
					break;
				}
				CurrentSourceSide = DestSideFunc(SweptRect);
				FVector2D SampleSpot = WidgetRect.GetCenter();
				SampleSpot[AxisIndex] = CurrentSourceSide;
				CurrentCellPoint = GetCellCoordinate(SampleSpot);
				bWrapped = true;
			}
			else if (NavigationReply.GetBoundaryRule() == EUINavigationRule::CustomBoundary)
			{
				const FNavigationDelegate& FocusDelegate = NavigationReply.GetFocusDelegate();
				if (FocusDelegate.IsBound())
				{
					return FocusDelegate.Execute(Direction);
				}
			}
		}
	}

	return TSharedPtr<SWidget>();
}

TSharedPtr<SWidget> FHittestGrid::FindNextFocusableWidget(const FArrangedWidget& StartingWidget, const EUINavigation Direction, const FNavigationReply& NavigationReply, const FArrangedWidget& RuleWidget)
{
	FGeometry StartingWidgetGeo = StartingWidget.Widget->GetPaintSpaceGeometry();
	StartingWidgetGeo.AppendTransform(FSlateLayoutTransform(-GridWindowOrigin));
	FSlateRect WidgetRect = StartingWidgetGeo.GetRenderBoundingRect();

	FGeometry BoundingRuleWidgetGeo = RuleWidget.Widget->GetPaintSpaceGeometry();
	BoundingRuleWidgetGeo.AppendTransform(FSlateLayoutTransform(-GridWindowOrigin));
	FSlateRect BoundingRuleRect = BoundingRuleWidgetGeo.GetRenderBoundingRect();

	FSlateRect SweptWidgetRect = WidgetRect;

	TSharedPtr<SWidget> Widget = TSharedPtr<SWidget>();

	switch (Direction)
	{
	case EUINavigation::Left:
		SweptWidgetRect.Left = BoundingRuleRect.Left;
		SweptWidgetRect.Right = BoundingRuleRect.Right;
		SweptWidgetRect.Top += 0.5f;
		SweptWidgetRect.Bottom -= 0.5f;
		Widget = FindFocusableWidget(WidgetRect, SweptWidgetRect, 0, -1, Direction, NavigationReply,
			[](float A, float B) { return A - 0.1f < B; }, // Compare function
			[](FSlateRect SourceRect) { return SourceRect.Left; }, // Source side function
			[](FSlateRect DestRect) { return DestRect.Right; }); // Dest side function
		break;
	case EUINavigation::Right:
		SweptWidgetRect.Left = BoundingRuleRect.Left;
		SweptWidgetRect.Right = BoundingRuleRect.Right;
		SweptWidgetRect.Top += 0.5f;
		SweptWidgetRect.Bottom -= 0.5f;
		Widget = FindFocusableWidget(WidgetRect, SweptWidgetRect, 0, 1, Direction, NavigationReply,
			[](float A, float B) { return A + 0.1f > B; }, // Compare function
			[](FSlateRect SourceRect) { return SourceRect.Right; }, // Source side function
			[](FSlateRect DestRect) { return DestRect.Left; }); // Dest side function
		break;
	case EUINavigation::Up:
		SweptWidgetRect.Top = BoundingRuleRect.Top;
		SweptWidgetRect.Bottom = BoundingRuleRect.Bottom;
		SweptWidgetRect.Left += 0.5f;
		SweptWidgetRect.Right -= 0.5f;
		Widget = FindFocusableWidget(WidgetRect, SweptWidgetRect, 1, -1, Direction, NavigationReply,
			[](float A, float B) { return A - 0.1f < B; }, // Compare function
			[](FSlateRect SourceRect) { return SourceRect.Top; }, // Source side function
			[](FSlateRect DestRect) { return DestRect.Bottom; }); // Dest side function
		break;
	case EUINavigation::Down:
		SweptWidgetRect.Top = BoundingRuleRect.Top;
		SweptWidgetRect.Bottom = BoundingRuleRect.Bottom;
		SweptWidgetRect.Left += 0.5f;
		SweptWidgetRect.Right -= 0.5f;
		Widget = FindFocusableWidget(WidgetRect, SweptWidgetRect, 1, 1, Direction, NavigationReply,
			[](float A, float B) { return A + 0.1f > B; }, // Compare function
			[](FSlateRect SourceRect) { return SourceRect.Bottom; }, // Source side function
			[](FSlateRect DestRect) { return DestRect.Top; }); // Dest side function
		break;

	default:
		break;
	}

	return Widget;
}

FIntPoint FHittestGrid::GetCellCoordinate(FVector2D Position)
{
	return FIntPoint(
		FMath::Min(FMath::Max(FMath::FloorToInt(Position.X / CellSize.X), 0), NumCells.X - 1),
		FMath::Min(FMath::Max(FMath::FloorToInt(Position.Y / CellSize.Y), 0), NumCells.Y - 1));
}

bool FHittestGrid::IsValidCellCoord(const FIntPoint& CellCoord) const
{
	return IsValidCellCoord(CellCoord.X, CellCoord.Y);
}

bool FHittestGrid::IsValidCellCoord(const int32 XCoord, const int32 YCoord) const
{
	return XCoord >= 0 && XCoord < NumCells.X && YCoord >= 0 && YCoord < NumCells.Y;
}

void FHittestGrid::AppendGrid(FHittestGrid& OtherGrid)
{
	// The two grids must occupy the same space
	if (ensure(this != &OtherGrid && GridOrigin == OtherGrid.GridOrigin && GridWindowOrigin == OtherGrid.GridWindowOrigin && GridSize == OtherGrid.GridSize))
	{
		// Index is not valid in the other grid so remap it 
		for (auto& Pair : OtherGrid.WidgetMap)
		{
			int32& WidgetIndex = WidgetMap.Add(Pair.Key);
			
			// Get the already created widget data out of the other widget array
			const FWidgetData& OtherData = OtherGrid.WidgetArray[Pair.Value];

			WidgetIndex = WidgetArray.Add(OtherData); 

			// Add the new widget index to the correct cells in this grid
			for (int32 XIndex = OtherData.UpperLeftCell.X; XIndex <= OtherData.LowerRightCell.X; ++XIndex)
			{
				for (int32 YIndex = OtherData.UpperLeftCell.Y; YIndex <= OtherData.LowerRightCell.Y; ++YIndex)
				{
					if (IsValidCellCoord(XIndex, YIndex))
					{
						CellAt(XIndex, YIndex).AddIndex(WidgetIndex);
					}
				}
			}

		}
	}
}
#if WITH_SLATE_DEBUGGING
void FHittestGrid::LogGrid() const
{
	FString TempString;
	for (int y = 0; y < NumCells.Y; ++y)
	{
		for (int x = 0; x < NumCells.X; ++x)
		{
			TempString += "\t";
			TempString += "[";
			for (int i : CellAt(x, y).GetCachedWidgetIndexes())
			{
				TempString += FString::Printf(TEXT("%d,"), i);
			}
			TempString += "]";
		}
		TempString += "\n";
	}

	TempString += "\n";

	UE_LOG(LogHittestDebug, Warning, TEXT("\n%s"), *TempString);

	for (TSparseArray<FWidgetData>::TConstIterator It(WidgetArray); It; ++It)
	{
		const FWidgetData& CurWidgetData = *It;
		const TSharedPtr<SWidget> CachedWidget = CurWidgetData.GetWidget();
		UE_LOG(LogHittestDebug, Warning, TEXT("  [%d][%d][%d] => %s @ %s"),
			It.GetIndex(),
			CurWidgetData.PrimarySort,
			CachedWidget.IsValid() ? *CachedWidget->ToString() : TEXT("Invalid WIdget"),
			CachedWidget.IsValid() ? *CachedWidget->GetPaintSpaceGeometry().ToString() : TEXT("Invalid WIdget"));
	}
}

void FHittestGrid::DisplayGrid(int32 InLayer, const FGeometry& AllottedGeometry, FSlateWindowElementList& WindowElementList) const
{
	static const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("FocusRectangle"));

	//WindowElementList.PushAbsoluteBatchPriortyGroup(INT_MAX);
	for (TSparseArray<FWidgetData>::TConstIterator It(WidgetArray); It; ++It)
	{
		const FWidgetData& CurWidgetData = *It;
		const TSharedPtr<SWidget> CachedWidget = CurWidgetData.GetWidget();
		if (CachedWidget.IsValid())
		{
			FSlateDrawElement::MakeBox(
				WindowElementList,
				InLayer,
				CachedWidget->GetPaintSpaceGeometry().ToPaintGeometry(),
				WhiteBrush
			);
		}
	}

	//WindowElementList.PopBatchPriortyGroup();
}

#endif // WITH_SLATE_DEBUGGING


void FHittestGrid::AddWidget(const TSharedRef<SWidget>& InWidget, int32 InBatchPriorityGroup, int32 InLayerId, int32 InSecondarySort)
{
	if (!InWidget->GetVisibility().IsHitTestVisible())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_SlateHTG_AddWidget);

	if (WidgetMap.Contains(&*InWidget))
	{
		// TODO: Update can be faster than a Remove and Add
		RemoveWidget(InWidget);
	}

	const int64 PrimarySort = (((int64)InBatchPriorityGroup << 32) | InLayerId);

	// Track the widget and identify it's Widget Index
	FGeometry GridSpaceGeometry = InWidget->GetPaintSpaceGeometry();
	GridSpaceGeometry.AppendTransform(FSlateLayoutTransform(-GridWindowOrigin));

	// Currently using grid offset because the grid covers all desktop space.
	const FSlateRect BoundingRect = GridSpaceGeometry.GetRenderBoundingRect();

	// Starting and ending cells covered by this widget.	
	const FIntPoint UpperLeftCell = GetCellCoordinate(BoundingRect.GetTopLeft());
	const FIntPoint LowerRightCell = GetCellCoordinate(BoundingRect.GetBottomRight());

	int32& WidgetIndex = WidgetMap.Add(&*InWidget);
	FWidgetData Data(InWidget, UpperLeftCell, LowerRightCell, PrimarySort, InSecondarySort);
	WidgetIndex = WidgetArray.Add(Data); // Bleh why doesn't Sparse Array have an emplace.

	for (int32 XIndex = UpperLeftCell.X; XIndex <= LowerRightCell.X; ++XIndex)
	{
		for (int32 YIndex = UpperLeftCell.Y; YIndex <= LowerRightCell.Y; ++YIndex)
		{
			if (IsValidCellCoord(XIndex, YIndex))
			{
				CellAt(XIndex, YIndex).AddIndex(WidgetIndex);
			}
		}
	}
}

void FHittestGrid::RemoveWidget(const TSharedRef<SWidget>& InWidget)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateHTG_RemoveWidget);

	int32 WidgetIndex = INDEX_NONE;
	if(WidgetMap.RemoveAndCopyValue(&*InWidget, WidgetIndex))
	{
		const FWidgetData& WidgetData = WidgetArray[WidgetIndex];

		// Starting and ending cells covered by this widget.	
		const FIntPoint& UpperLeftCell = WidgetData.UpperLeftCell;
		const FIntPoint& LowerRightCell = WidgetData.LowerRightCell;

		for (int32 XIndex = UpperLeftCell.X; XIndex <= LowerRightCell.X; ++XIndex)
		{
			for (int32 YIndex = UpperLeftCell.Y; YIndex <= LowerRightCell.Y; ++YIndex)
			{
				checkSlow(IsValidCellCoord(XIndex, YIndex));
				CellAt(XIndex, YIndex).RemoveIndex(WidgetIndex);
			}
		}

		WidgetArray.RemoveAt(WidgetIndex);
	}
}

void FHittestGrid::InsertCustomHitTestPath(const TSharedRef<SWidget> InWidget, TSharedRef<ICustomHitTestPath> CustomHitTestPath)
{
	int32 WidgetIndex = WidgetMap.FindChecked(&*InWidget);
	FWidgetData& WidgetData = WidgetArray[WidgetIndex];
	WidgetData.CustomPath = CustomHitTestPath;
}

FHittestGrid::FIndexAndDistance FHittestGrid::GetHitIndexFromCellIndex(const FGridTestingParams& Params)
{
	//check if the cell coord 
	if (IsValidCellCoord(Params.CellCoord))
	{
		// Get the cell and sort it 
		FHittestGrid::FCell& Cell = CellAt(Params.CellCoord.X, Params.CellCoord.Y);
		Cell.Sort(WidgetArray);

		// Get the cell's index array for the search
		const TArray<int32>& IndexesInCell = Cell.GetCachedWidgetIndexes();

#if 0 //Unroll some data for debugging if necessary
		struct FDebugData
		{
			int32 WidgetIndex;
			int64 PrimarySort;
			FName WidgetType;
			FName WidgetLoc;
			TSharedPtr<SWidget> Widget;
		};

		TArray<FDebugData> DebugData;
		for (int32 i = 0; i < IndexesInCell.Num(); ++i)
		{
			FDebugData& Cur = DebugData.AddDefaulted_GetRef();
			Cur.WidgetIndex = IndexesInCell[i];
			Cur.PrimarySort = WidgetArray[Cur.WidgetIndex].PrimarySort;
			Cur.Widget = WidgetArray[Cur.WidgetIndex].GetWidget();
			Cur.WidgetType = Cur.Widget.IsValid() ? Cur.Widget->GetType() : NAME_None;
			Cur.WidgetLoc = Cur.Widget.IsValid() ? Cur.Widget->GetCreatedInLocation() : NAME_None;
		}

#endif

		// Consider front-most widgets first for hittesting.
		for (int32 i = IndexesInCell.Num() - 1; i >= 0; --i)
		{
			const int32 WidgetIndex = IndexesInCell[i];

			checkSlow(WidgetArray.IsValidIndex(WidgetIndex));

			const FWidgetData& TestCandidate = WidgetArray[WidgetIndex];
			const TSharedPtr<SWidget> TestWidget = TestCandidate.GetWidget();

			// When performing a point hittest, accept all hittestable widgets.
			// When performing a hittest with a radius, only grab interactive widgets.
			const bool bIsValidWidget = TestWidget.IsValid() && (!Params.bTestWidgetIsInteractive || TestWidget->IsInteractable());
			if (bIsValidWidget)
			{
				const FVector2D WindowSpaceCoordinate = Params.CursorPositionInGrid + GridWindowOrigin;

				const FGeometry& TestGeometry = TestWidget->GetPaintSpaceGeometry();

				bool bPointInsideClipMasks = true;
				const TOptional<FSlateClippingState>& WidgetClippingState = TestWidget->GetCurrentClippingState();
				if (WidgetClippingState.IsSet())
				{
					// TODO: Solve non-zero radius cursors?
					bPointInsideClipMasks = WidgetClippingState->IsPointInside(WindowSpaceCoordinate);
				}

				if (bPointInsideClipMasks)
				{
					// Compute the render space clipping rect (FGeometry exposes a layout space clipping rect).
					const FSlateRotatedRect WindowOrientedClipRect = TransformRect(
						Concatenate(
							Inverse(TestGeometry.GetAccumulatedLayoutTransform()),
							TestGeometry.GetAccumulatedRenderTransform()),
						FSlateRotatedRect(TestGeometry.GetLayoutBoundingRect())
					);

					if (IsOverlappingSlateRotatedRect(WindowSpaceCoordinate, Params.Radius, WindowOrientedClipRect))
					{
						// For non-0 radii also record the distance to cursor's center so that we can pick the closest hit from the results.
						const bool bNeedsDistanceSearch = Params.Radius > 0.0f;
						const float DistSq = (bNeedsDistanceSearch) ? DistanceSqToSlateRotatedRect(WindowSpaceCoordinate, WindowOrientedClipRect) : 0.0f;
						return FIndexAndDistance(WidgetIndex, DistSq);
					}
				}
			}
		}
	}

	return FIndexAndDistance();
}