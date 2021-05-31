// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Set of templated functions operating on common APIed lens table data structure
 */
namespace LensDataTableUtils
{
	/** Removes a focus point from a container */
	template<typename FocusPointType>
	void RemoveFocusPoint(TArray<FocusPointType>& Container, float InFocus)
	{
		const int32 FoundIndex = Container.IndexOfByPredicate([InFocus](const FocusPointType& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
    	if(FoundIndex != INDEX_NONE)
    	{
    		Container.RemoveAt(FoundIndex);
    	}
	}

	/** Removes a zoom point for a given focus value in a container */
	template<typename FocusPointType>
	void RemoveZoomPoint(TArray<FocusPointType>& Container, float InFocus, float InZoom)
	{
		bool bIsEmpty = false;
		const int32 FoundIndex = Container.IndexOfByPredicate([InFocus](const FocusPointType& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
		if(FoundIndex != INDEX_NONE)
		{
			Container[FoundIndex].RemovePoint(InZoom);
			bIsEmpty = Container[FoundIndex].IsEmpty();
		}

		if(bIsEmpty)
		{
			Container.RemoveAt(FoundIndex);
		}
	}

	/** Adds a point at a specified focus and zoom input values */
	template<typename FocusPointType, typename DataType>
	bool AddPoint(TArray<FocusPointType>& InContainer, float InFocus, float InZoom, const DataType& InData, float InputTolerance, bool bIsCalibrationPoint)
	{
		int32 PointIndex = 0;
		for (; PointIndex < InContainer.Num() && InContainer[PointIndex].Focus <= InFocus; ++PointIndex)
		{
			FocusPointType& FocusPoint = InContainer[PointIndex];
			if (FMath::IsNearlyEqual(FocusPoint.Focus, InFocus, InputTolerance))
			{
				return FocusPoint.AddPoint(InZoom, InData, InputTolerance, bIsCalibrationPoint);
			}
		}

		FocusPointType NewFocusPoint;
		NewFocusPoint.Focus = InFocus;
		const bool bSuccess = NewFocusPoint.AddPoint(InZoom, InData, InputTolerance, bIsCalibrationPoint);
		if(bSuccess)
		{
			InContainer.Insert(MoveTemp(NewFocusPoint), PointIndex);
		}

		return bSuccess;
	}

	/** Clears content of a table */
	template<typename Type>
	void EmptyTable(Type& InTable)
	{
		InTable.FocusPoints.Empty(0);
	}

		struct FPointNeighbors
	{
		int32 PreviousIndex = INDEX_NONE;
		int32 NextIndex = INDEX_NONE;
	};

	/** Finds indices of neighbor focus points for a given focus value */
	template<typename Type>
	FPointNeighbors FindFocusPoints(float InFocus, TConstArrayView<Type> Container)
	{
		FPointNeighbors Neighbors;
		if (Container.Num() <= 0)
		{
			return Neighbors;
		}

		for (int32 Index = 0; Index < Container.Num(); ++Index)
		{
			const Type& Point = Container[Index];
			if (Point.Focus > InFocus)
			{
				Neighbors.NextIndex = Index;
				Neighbors.PreviousIndex = FMath::Max(Index - 1, 0);
				break;
			}
			else if (FMath::IsNearlyEqual(Point.Focus, InFocus))
			{
				//We found a point exactly matching the desired one
				Neighbors.NextIndex = Index;
				Neighbors.PreviousIndex = Index;
				break;
			}
		}

		//We haven't found a point, default to last one
		if (Neighbors.PreviousIndex == INDEX_NONE && Neighbors.NextIndex == INDEX_NONE)
		{
			Neighbors.NextIndex = Container.Num() - 1;
			Neighbors.PreviousIndex = Container.Num() - 1;
		}

		return Neighbors;
	}

	/** Finds indices of neighbor zoom points for a given zoom value */
	template<typename Type>
	FPointNeighbors FindZoomPoints(float InZoom, const TArray<Type>& Container)
	{
		FPointNeighbors Neighbors;
		if (Container.Num() <= 0)
		{
			return Neighbors;
		}

		for (int32 Index = 0; Index < Container.Num(); ++Index)
		{
			const Type& Point = Container[Index];
			if (Point.Zoom > InZoom)
			{
				Neighbors.NextIndex = Index;
				Neighbors.PreviousIndex = FMath::Max(Index - 1, 0);
				break;
			}
			else if (FMath::IsNearlyEqual(Point.Zoom, InZoom))
			{
				//We found a point exactly matching the desired one
				Neighbors.NextIndex = Index;
				Neighbors.PreviousIndex = Index;
				break;
			}
		}

		//We haven't found a point, default to last one
		if (Neighbors.PreviousIndex == INDEX_NONE && Neighbors.NextIndex == INDEX_NONE)
		{
			Neighbors.NextIndex = Container.Num() - 1;
			Neighbors.PreviousIndex = Container.Num() - 1;
		}

		return Neighbors;
	}

	/** Finds a points that matches input Focus and Zoom and returns its value.
	 * Returns false if point isn't found
	 */
	template<typename FocusPointType, typename DataType>
	bool GetPointValue(float InFocus, float InZoom, TConstArrayView<FocusPointType> Container, DataType& OutData)
	{
		const FPointNeighbors FocusNeighbors = FindFocusPoints(InFocus, Container);
		if(FocusNeighbors.PreviousIndex == FocusNeighbors.NextIndex)
		{
			const FPointNeighbors ZoomNeighbors = FindZoomPoints(InZoom, Container[FocusNeighbors.PreviousIndex].ZoomPoints);
			if(ZoomNeighbors.PreviousIndex == ZoomNeighbors.NextIndex)
			{
				return Container[FocusNeighbors.PreviousIndex].GetValue(ZoomNeighbors.PreviousIndex, OutData);
			}
		}

		return false;
	}
}
