// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"
#include "Layout/ArrangedWidget.h"
#include "Layout/Clipping.h"
#include "Input/Events.h"
#include "Widgets/SWidget.h"

class FArrangedChildren;

class ICustomHitTestPath
{
public:
	virtual ~ICustomHitTestPath(){}

	virtual TArray<FWidgetAndPointer> GetBubblePathAndVirtualCursors( const FGeometry& InGeometry, FVector2D DesktopSpaceCoordinate, bool bIgnoreEnabledStatus ) const = 0;

	virtual void ArrangeChildren( FArrangedChildren& ArrangedChildren ) const = 0;

	virtual TSharedPtr<struct FVirtualPointerPosition> TranslateMouseCoordinateFor3DChild( const TSharedRef<SWidget>& ChildWidget, const FGeometry& ViewportGeometry, const FVector2D& ScreenSpaceMouseCoordinate, const FVector2D& LastScreenSpaceMouseCoordinate ) const = 0;
};

class SLATECORE_API FHittestGrid
{
public:

	FHittestGrid();
	~FHittestGrid();

	/**
	 * Given a Slate Units coordinate in virtual desktop space, perform a hittest
	 * and return the path along which the corresponding event would be bubbled.
	 */
	TArray<FWidgetAndPointer> GetBubblePath(FVector2D DesktopSpaceCoordinate, float CursorRadius, bool bIgnoreEnabledStatus, int32 UserIndex = INDEX_NONE);

	/**
	 * Set the position and size of the hittest area in desktop coordinates
	 *
	 * @param HittestPositionInDesktop	The position of this hit testing area in desktop coordinates.
	 * @param HittestDimensions			The dimensions of this hit testing area.
	 *
	 * @return      Returns true if a clear of the hittest grid was required. 
	 */
	bool SetHittestArea(const FVector2D& HittestPositionInDesktop, const FVector2D& HittestDimensions, const FVector2D& HitestOffsetInWindow = FVector2D::ZeroVector);

	/**
	 * Clear the grid
	 */
	void Clear();

	/** Add / Remove SWidget from the HitTest Grid */
	void AddWidget(const TSharedRef<SWidget>& InWidget, int32 InBatchPriorityGroup, int32 InLayerId, int32 InSecondarySort);
	void RemoveWidget(const TSharedRef<SWidget>& InWidget);

	/** Insert custom hit test data for a widget already in the grid */
	void InsertCustomHitTestPath(const TSharedRef<SWidget> InWidget, TSharedRef<ICustomHitTestPath> CustomHitTestPath);

	/** Sets the current slate user index that should be associated with any added widgets */
	void SetUserIndex(int32 UserIndex) { CurrentUserIndex = UserIndex; }

	/** Gets current slate user index that should be associated with any added widgets */
	int32 GetUserIndex() const { return CurrentUserIndex; }

	/**
	 * Finds the next focusable widget by searching through the hit test grid
	 *
	 * @param StartingWidget  This is the widget we are starting at, and navigating from.
	 * @param Direction       The direction we should search in.
	 * @param NavigationReply The Navigation Reply to specify a boundary rule for the search.
	 * @param RuleWidget      The Widget that is applying the boundary rule, used to get the bounds of the Rule.
	 */
	TSharedPtr<SWidget> FindNextFocusableWidget(const FArrangedWidget& StartingWidget, const EUINavigation Direction, const FNavigationReply& NavigationReply, const FArrangedWidget& RuleWidget, int32 UserIndex);

	void AppendGrid(FHittestGrid& OtherGrid);

	FVector2D GetGridSize() const { return GridSize; }
	FVector2D GetGridOrigin() const { return GridOrigin; }
	FVector2D GetGridWindowOrigin() const { return GridWindowOrigin; }

#if WITH_SLATE_DEBUGGING
	void LogGrid() const;

	void DisplayGrid(int32 InLayer, const FGeometry& AllottedGeometry, FSlateWindowElementList& WindowElementList) const;
#endif

private:

	/**
	 * Widget Data we maintain internally store along with the widget reference
	 */
	struct FWidgetData
	{
		FWidgetData(TSharedRef<SWidget> InWidget, const FIntPoint& InUpperLeftCell, const FIntPoint& InLowerRightCell, int64 InPrimarySort, int32 InSecondarySort, int32 InUserIndex)
			: WeakWidget(InWidget)
			, UpperLeftCell(InUpperLeftCell)
			, LowerRightCell(InLowerRightCell)
			, PrimarySort(InPrimarySort)
			, SecondarySort(InSecondarySort)
			, UserIndex(InUserIndex)
		{}
		TWeakPtr<SWidget> WeakWidget;
		TWeakPtr<ICustomHitTestPath> CustomPath;
		FIntPoint UpperLeftCell;
		FIntPoint LowerRightCell;
		int64 PrimarySort;
		int32 SecondarySort;
		int32 UserIndex;

		TSharedPtr<SWidget> GetWidget() const { return WeakWidget.Pin(); }
	};

	/**
	 * All the available space is partitioned into Cells.
	 * Each Cell contains a list of widgets that overlap the cell.
	 * The list is ordered from back to front.
	 */
	struct FCell
	{
		FCell();

		void AddIndex(int32 WidgetIndex);
		void RemoveIndex(int32 WidgetIndex);
		void Sort(const TSparseArray<FWidgetData>& InWidgetArray);

		const TArray<int32>& GetCachedWidgetIndexes() const;
		
	private:
		bool bRequiresSort;
		TArray<int32> CachedWidgetIndexes;
	};

	struct FGridTestingParams;

	struct FIndexAndDistance
	{
		FIndexAndDistance(int32 InIndex = INDEX_NONE, float InDistanceSq = 0)
			: WidgetIndex(InIndex)
			, DistanceSqToWidget(InDistanceSq)
		{}
		int32 WidgetIndex;
		float DistanceSqToWidget;
	};

	/** Helper functions */
	bool IsValidCellCoord(const FIntPoint& CellCoord) const;
	bool IsValidCellCoord(const int32 XCoord, const int32 YCoord) const;

	/** Return the Index and distance to a hit given the testing params */
	FIndexAndDistance GetHitIndexFromCellIndex(const FGridTestingParams& Params);

	/** @returns true if the child is a paint descendant of the provided Parent. */
	bool IsDescendantOf(const TSharedRef<SWidget> Parent, const FWidgetData& ChildData);

	/** Utility function for searching for the next focusable widget. */
	template<typename TCompareFunc, typename TSourceSideFunc, typename TDestSideFunc>
	TSharedPtr<SWidget> FindFocusableWidget(const FSlateRect WidgetRect, const FSlateRect SweptRect, int32 AxisIndex, int32 Increment, const EUINavigation Direction, const FNavigationReply& NavigationReply, TCompareFunc CompareFunc, TSourceSideFunc SourceSideFunc, TDestSideFunc DestSideFunc, int32 UserIndex);

	/** Constrains a float position into the grid coordinate. */
	FIntPoint GetCellCoordinate(FVector2D Position);

	/** Access a cell at coordinates X, Y. Coordinates are row and column indexes. */
	FORCEINLINE_DEBUGGABLE FCell& CellAt(const int32 X, const int32 Y)
	{
		checkfSlow((Y*NumCells.X + X) < Cells.Num(), TEXT("HitTestGrid CellAt() failed: X= %d Y= %d NumCells.X= %d NumCells.Y= %d Cells.Num()= %d"), X, Y, NumCells.X, NumCells.Y, Cells.Num());
		return Cells[Y*NumCells.X + X];
	}

	/** Access a cell at coordinates X, Y. Coordinates are row and column indexes. */
	FORCEINLINE_DEBUGGABLE const FCell& CellAt( const int32 X, const int32 Y ) const
	{
		checkfSlow((Y*NumCells.X + X) < Cells.Num(), TEXT("HitTestGrid CellAt() failed: X= %d Y= %d NumCells.X= %d NumCells.Y= %d Cells.Num()= %d"), X, Y, NumCells.X, NumCells.Y, Cells.Num());
		return Cells[Y*NumCells.X + X];
	}

	/** Map of all the widgets currently in the hit test grid to their stable index. */
	TMap<SWidget*, int32> WidgetMap;

	/** Stable indexed sparse array of all the widget data we track. */
	TSparseArray<FWidgetData> WidgetArray;

	/** The cells that make up the space partition. */
	TArray<FCell> Cells;

	/** The size of the grid in cells. */
	FIntPoint NumCells;

	/** Where the 0,0 of the upper-left-most cell corresponds to in desktop space. */
	FVector2D GridOrigin;

	/** Where the 0,0 of the upper-left-most cell corresponds to in window space. */
	FVector2D GridWindowOrigin;

	/** The Size of the current grid. */
	FVector2D GridSize;

	/** The current slate user index that should be associated with any added widgets */
	int32 CurrentUserIndex;
};
