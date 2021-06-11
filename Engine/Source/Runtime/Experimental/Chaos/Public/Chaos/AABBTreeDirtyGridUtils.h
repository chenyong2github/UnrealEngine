// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/AABB.h"
#include "Chaos/Defines.h"
#include "ChaosLog.h"

namespace Chaos
{

	FORCEINLINE_DEBUGGABLE uint32 InterleaveWithZeros(uint16 input)
	{
		uint32 Intermediate = (uint32)input;
		Intermediate = (Intermediate ^ (Intermediate << 8)) & 0x00ff00ff;
		Intermediate = (Intermediate ^ (Intermediate << 4)) & 0x0f0f0f0f;
		Intermediate = (Intermediate ^ (Intermediate << 2)) & 0x33333333;
		Intermediate = (Intermediate ^ (Intermediate << 1)) & 0x55555555;
		return Intermediate;
	}

	FORCEINLINE_DEBUGGABLE int32 HashCoordinates(FReal Xcoordinate, FReal Ycoordinate, FReal DirtyElementGridCellSizeInv)
	{
		// Requirement: Hash should change for adjacent cells
		uint16 X = (uint16)FMath::Floor(Xcoordinate * DirtyElementGridCellSizeInv);
		uint16 Y = (uint16)FMath::Floor(Ycoordinate * DirtyElementGridCellSizeInv);

		return (int32)(InterleaveWithZeros(X) | (InterleaveWithZeros(Y) << 1));
	}

	FORCEINLINE_DEBUGGABLE int32 GetDirtyCellIndexFromWorldCoordinate(FReal Coordinate, FReal DirtyElementGridCellSizeInv)
	{
		return (int32)(FMath::Floor(Coordinate * DirtyElementGridCellSizeInv));
	}

	FORCEINLINE_DEBUGGABLE bool TooManyOverlapQueryCells(const TAABB<FReal, 3>& AABB, FReal DirtyElementGridCellSizeInv, int32 MaximumOverlap)
	{
		int32 XsampleCount = (int32)(FMath::Floor(AABB.Max().X * DirtyElementGridCellSizeInv)) - (int32)(FMath::Floor(AABB.Min().X * DirtyElementGridCellSizeInv)) + 1;
		int32 YsampleCount = (int32)(FMath::Floor(AABB.Max().Y * DirtyElementGridCellSizeInv)) - (int32)(FMath::Floor(AABB.Min().Y * DirtyElementGridCellSizeInv)) + 1;

		if (XsampleCount * YsampleCount <= MaximumOverlap)
		{
			return false;
		}
		return true;
	}

	template <typename FunctionType>
	FORCEINLINE_DEBUGGABLE void DoForOverlappedCells(const TAABB<FReal, 3>& AABB, FReal DirtyElementGridCellSize, FReal DirtyElementGridCellSizeInv, FunctionType Function)
	{
		int32 XsampleCount = (int32)(FMath::Floor(AABB.Max().X * DirtyElementGridCellSizeInv)) - (int32)(FMath::Floor(AABB.Min().X * DirtyElementGridCellSizeInv)) + 1;
		int32 YsampleCount = (int32)(FMath::Floor(AABB.Max().Y * DirtyElementGridCellSizeInv)) - (int32)(FMath::Floor(AABB.Min().Y * DirtyElementGridCellSizeInv)) + 1;

		FReal CurrentX = AABB.Min().X;
		for (int32 XsampleIndex = 0; XsampleIndex < XsampleCount; XsampleIndex++)
		{
			FReal CurrentY = AABB.Min().Y;
			for (int32 YsampleIndex = 0; YsampleIndex < YsampleCount; YsampleIndex++)
			{
				Function(HashCoordinates(CurrentX, CurrentY, DirtyElementGridCellSizeInv));
				CurrentY += DirtyElementGridCellSize;
			}
			CurrentX += DirtyElementGridCellSize;
		}
	}

	// Only execute function for new Cells not covered in old (Set difference: {Cells spanned by AABB} - { Cells spanned by AABBExclude})
	template <typename FunctionType>
	FORCEINLINE_DEBUGGABLE bool DoForOverlappedCellsExclude(const TAABB<FReal, 3>& AABB, const TAABB<FReal, 3>& AABBExclude, FReal DirtyElementGridCellSize, FReal DirtyElementGridCellSizeInv, FunctionType Function)
	{

		int32 NewCellStartX = GetDirtyCellIndexFromWorldCoordinate(AABB.Min().X, DirtyElementGridCellSizeInv);
		int32 NewCellStartY = GetDirtyCellIndexFromWorldCoordinate(AABB.Min().Y, DirtyElementGridCellSizeInv);

		int32 NewCellEndX = GetDirtyCellIndexFromWorldCoordinate(AABB.Max().X, DirtyElementGridCellSizeInv);
		int32 NewCellEndY = GetDirtyCellIndexFromWorldCoordinate(AABB.Max().Y, DirtyElementGridCellSizeInv);

		int32 OldCellStartX = GetDirtyCellIndexFromWorldCoordinate(AABBExclude.Min().X, DirtyElementGridCellSizeInv);
		int32 OldCellStartY = GetDirtyCellIndexFromWorldCoordinate(AABBExclude.Min().Y, DirtyElementGridCellSizeInv);

		int32 OldCellEndX = GetDirtyCellIndexFromWorldCoordinate(AABBExclude.Max().X, DirtyElementGridCellSizeInv);
		int32 OldCellEndY = GetDirtyCellIndexFromWorldCoordinate(AABBExclude.Max().Y, DirtyElementGridCellSizeInv);

		// Early out here
		if (OldCellStartX <= NewCellStartX &&
			OldCellStartY <= NewCellStartY &&
			OldCellEndX >= NewCellEndX &&
			OldCellEndY >= NewCellEndY)
		{
			return true;
		}

		for (int32 X = NewCellStartX; X <= NewCellEndX; X++)
		{
			for (int32 Y = NewCellStartY; Y <= NewCellEndY; Y++)
			{
				if (!(X >= OldCellStartX && X <= OldCellEndX && Y >= OldCellStartY && Y <= OldCellEndY))
				{
					if (!Function(HashCoordinates((FReal)X * DirtyElementGridCellSize, (FReal)Y * DirtyElementGridCellSize, DirtyElementGridCellSizeInv)))
						return false; // Failure early out
				}
			}
		}
		return true;
	}

	FORCEINLINE_DEBUGGABLE int32 FindInSortedArray(const TArray<int32>& Array, int32 FindValue, int32 StartIndex, int32 EndIndex)
	{
		int32 TestIndex = (EndIndex + StartIndex) / 2;
		int32 TestValue = Array[TestIndex];
		if (TestValue == FindValue)
		{
			return TestIndex;
		}

		if (StartIndex == EndIndex)
		{
			return INDEX_NONE;
		}

		if (TestValue < FindValue)
		{
			// tail-recursion
			return FindInSortedArray(Array, FindValue, TestIndex + 1, EndIndex);
		}

		if (StartIndex == TestIndex)
		{
			return INDEX_NONE;
		}

		// tail-recursion
		return FindInSortedArray(Array, FindValue, StartIndex, TestIndex - 1);
	}

	FORCEINLINE_DEBUGGABLE int32 FindInsertIndexIntoSortedArray(const TArray<int32>& Array, int32 FindValue, int32 StartIndex, int32 EndIndex)
	{
		int32 TestIndex = (EndIndex + StartIndex) / 2;
		int32 TestValue = Array[TestIndex];
		if (TestValue == FindValue)
		{
			return INDEX_NONE; // Already in array
		}

		if (StartIndex == EndIndex)
		{
			return FindValue > TestValue ? StartIndex + 1 : StartIndex;
		}

		if (TestValue < FindValue)
		{
			// tail-recursion
			return FindInsertIndexIntoSortedArray(Array, FindValue, TestIndex + 1, EndIndex);
		}

		if (StartIndex == TestIndex)
		{
			return TestIndex;
		}

		// tail-recursion
		return FindInsertIndexIntoSortedArray(Array, FindValue, StartIndex, TestIndex - 1);
	}

	// Prerequisites: The array must be sorted from StartIndex to StartIndex + Count -1, and must have one element past StartIndex + Count -1 allocated
	// returns false if the value was already in the array and therefore not added again
	FORCEINLINE_DEBUGGABLE bool InsertValueIntoSortedSubArray(TArray<int32>& Array, int32 Value, int32 StartIndex, int32 Count)
	{
		// We must keep everything sorted
		if (Count > 0)
		{
			int32 EndIndex = StartIndex + Count - 1;
			int32 InsertIndex = FindInsertIndexIntoSortedArray(Array, Value, StartIndex, EndIndex);
			//ensure(InsertIndex != INDEX_NONE) // Just for debugging
			if (InsertIndex != INDEX_NONE)
			{
				for (int32 Index = EndIndex + 1; Index > InsertIndex; Index--)
				{
					Array[Index] = Array[Index - 1];
				}
				Array[InsertIndex] = Value;
			}
			else
			{
				return false;
			}
		}
		else
		{
			Array[StartIndex] = Value;
		}
		return true;
	}

	// Prerequisites: The array must be sorted from StartIndex to EndIndex. 
	// The extra element won't be deallocated
	// returns true if the element has been found and successfully deleted
	FORCEINLINE_DEBUGGABLE bool DeleteValueFromSortedSubArray(TArray<int32>& Array, int32 Value, int32 StartIndex, int32 Count)
	{
		if (ensure(Count > 0))
		{
			int32 EndIndex = StartIndex + Count - 1;
			int32 DeleteIndex = FindInSortedArray(Array, Value, StartIndex, EndIndex);
			if (DeleteIndex != INDEX_NONE)
			{
				for (int32 Index = DeleteIndex; Index < EndIndex; Index++)
				{
					Array[Index] = Array[Index + 1];
				}
				return true;
			}
		}
		return false;
	}

	FORCEINLINE_DEBUGGABLE bool TooManySweepQueryCells(const TVec3<FReal>& QueryHalfExtents, const TVector<FReal, 3>& StartPoint, const TVector<FReal, 3>& Dir, FReal Length, FReal DirtyElementGridCellSizeInv, int32 DirtyElementMaxGridCellQueryCount)
	{
		int EstimatedNumberOfCells = ((int32)(QueryHalfExtents.X * 2 * DirtyElementGridCellSizeInv) + 2) * ((int32)(QueryHalfExtents.Y * 2 * DirtyElementGridCellSizeInv) + 2) +
			((int32)(FMath::Max(QueryHalfExtents.X, QueryHalfExtents.Y) * 2 * DirtyElementGridCellSizeInv) + 2) * ((int32)(Length * DirtyElementGridCellSizeInv) + 2);

		return EstimatedNumberOfCells > DirtyElementMaxGridCellQueryCount;
	}


	// This function should be called with a dominant x direction only!
	template <typename FunctionType>
	FORCEINLINE_DEBUGGABLE void DoForSweepIntersectCellsImp(const FReal QueryHalfExtentsX, const FReal QueryHalfExtentsY, const FReal StartPointX, const FReal StartPointY, const FReal EndPointX, const FReal EndPointY, FReal DirtyElementGridCellSize, FReal DirtyElementGridCellSizeInv, FunctionType InFunction)
	{
		// Use 2 paths (Line0 and Line1) that traces the shape of the swept AABB and fill between them
		// Example of one of the cases (XDirectionDominant, Fill Up):
		//                            Line0
		//                        #############
		//                      #             
		//                    #               
		//            Line0 #                 
		//                #                   #
		//              #                   #
		//            #                   #   ----> Dx
		//          #       ^           #
		//        #         |         # Line1
		//                 Fill     #
		//                  |     #
		//                      #
		//        #############
		//           Line1    ^TurningPointForLine1


		FReal DeltaX = EndPointX - StartPointX;
		FReal DeltaY = EndPointY - StartPointY;

		FReal AbsDx = FMath::Abs(DeltaX);
		FReal AbsDy = FMath::Abs(DeltaY);

		bool DxTooSmall = AbsDx <= SMALL_NUMBER;
		bool DyTooSmall = AbsDy <= SMALL_NUMBER;

		int DeltaCelIndexX;
		int DeltaCelIndexY;
		FReal DtDy = 0; // DeltaTime over DeltaX
		FReal DtDx = 0;

		if (DxTooSmall)
		{
			// This is just the overlap case (no casting along a ray)
			DeltaCelIndexX = 1;
			DeltaCelIndexY = 1;
		}
		else
		{
			DeltaCelIndexX = DeltaX >= 0 ? 1 : -1;
			DeltaCelIndexY = DeltaY >= 0 ? 1 : -1;
		}

		// Use parametric description of the lines here (t is the parameter and is positive along the ray)
		// x = Dx/Dt*t + x0
		// y = Dy/Dt*t + x0
		DtDx = (FReal)DeltaCelIndexX;
		DtDy = DyTooSmall ? 1 : (FReal)DeltaCelIndexX * DeltaX / DeltaY;

		// Calculate all the bounds we need
		FReal XEndPointExpanded = EndPointX + (DeltaCelIndexX >= 0 ? QueryHalfExtentsX : -QueryHalfExtentsX);
		FReal YEndPointExpanded = EndPointY + (DeltaCelIndexY >= 0 ? QueryHalfExtentsY : -QueryHalfExtentsY);
		FReal XStartPointExpanded = StartPointX + (DeltaCelIndexX >= 0 ? -QueryHalfExtentsX : QueryHalfExtentsX);
		FReal YStartPointExpanded = StartPointY + (DeltaCelIndexY >= 0 ? -QueryHalfExtentsY : QueryHalfExtentsY);
		int32 TurningPointForLine1; // This is where we need to change direction for line 2
		TurningPointForLine1 = GetDirtyCellIndexFromWorldCoordinate(StartPointX + (DeltaCelIndexX >= 0 ? QueryHalfExtentsX : -QueryHalfExtentsX), DirtyElementGridCellSizeInv);

		// Line0 current position
		FReal X0 = XStartPointExpanded;
		FReal Y0 = YStartPointExpanded + QueryHalfExtentsY * (FReal)DeltaCelIndexY * 2;

		// Line1 current position
		FReal X1 = XStartPointExpanded;
		FReal Y1 = YStartPointExpanded;

		int32 CurrentCellIndexX0 = (int32)FMath::Floor(X0 * DirtyElementGridCellSizeInv);
		int32 CurrentCellIndexY0 = (int32)FMath::Floor(Y0 * DirtyElementGridCellSizeInv);

		int32 CurrentCellIndexX1 = (int32)FMath::Floor(X1 * DirtyElementGridCellSizeInv);
		int32 CurrentCellIndexY1 = (int32)FMath::Floor(Y1 * DirtyElementGridCellSizeInv);

		int32 LastCellIndexX = (int32)FMath::Floor(XEndPointExpanded * DirtyElementGridCellSizeInv);
		int32 LastCellIndexY = (int32)FMath::Floor(YEndPointExpanded * DirtyElementGridCellSizeInv);

		bool Done = false;
		while (!Done)
		{
			// Advance Line 0 crossing a horizontal border here (angle is 45 degrees or less)

			if (CurrentCellIndexY0 * DeltaCelIndexY < LastCellIndexY * DeltaCelIndexY && !DyTooSmall)
			{
				FReal CrossingVerticleCellBorderT = std::numeric_limits<FReal>::max();
				FReal CrossingHorizontalCellBorderT = std::numeric_limits<FReal>::max();
				CrossingVerticleCellBorderT = DtDx * ((FReal)(CurrentCellIndexX0 + (DeltaCelIndexX > 0 ? 1 : 0)) * DirtyElementGridCellSize - X0);
				CrossingHorizontalCellBorderT = DtDy * ((FReal)(CurrentCellIndexY0 + (DeltaCelIndexY > 0 ? 1 : 0)) * DirtyElementGridCellSize - Y0);
				if (CrossingHorizontalCellBorderT < CrossingVerticleCellBorderT)
				{
					X0 += CrossingHorizontalCellBorderT * (1 / DtDx);  // DtDx is always 1 or -1
					Y0 += CrossingHorizontalCellBorderT * (1 / DtDy);  // Abs(DtDy) >= 1
					CurrentCellIndexY0 += DeltaCelIndexY;
				}
			}

			for (int CurrentFillCellIndexY = CurrentCellIndexY1; CurrentFillCellIndexY * DeltaCelIndexY <= CurrentCellIndexY0 * DeltaCelIndexY; CurrentFillCellIndexY += DeltaCelIndexY)
			{
				InFunction((FReal)CurrentCellIndexX0 * DirtyElementGridCellSize, (FReal)CurrentFillCellIndexY * DirtyElementGridCellSize);
			}

			// Advance line 0 crossing vertical cell borders
			{
				if (CurrentCellIndexY0 != LastCellIndexY && !DyTooSmall)
				{
					FReal CrossingVerticleCellBorderT = std::numeric_limits<FReal>::max();
					CrossingVerticleCellBorderT = DtDx * ((FReal)(CurrentCellIndexX0 + (DeltaCelIndexX > 0 ? 1 : 0)) * DirtyElementGridCellSize - X0);
					X0 += CrossingVerticleCellBorderT * (1 / DtDx);
					Y0 += CrossingVerticleCellBorderT * (1 / DtDy);
				}
				else
				{
					X0 += DirtyElementGridCellSize * (FReal)DeltaCelIndexX;
				}
				CurrentCellIndexX0 += DeltaCelIndexX;
			}

			// Advance line 1
			if (CurrentCellIndexX1 != LastCellIndexX)
			{
				if (CurrentCellIndexX1 * DeltaCelIndexX < TurningPointForLine1 * DeltaCelIndexX)
				{
					X1 += DirtyElementGridCellSize * (FReal)DeltaCelIndexX;
				}
				else
				{
					if (CurrentCellIndexX1 == TurningPointForLine1)
					{
						// Put Line position exactly at the turning point
						X1 = StartPointX + (DeltaCelIndexX >= 0 ? QueryHalfExtentsX : -QueryHalfExtentsX);
					}

					FReal CrossingVerticleCellBorderT = std::numeric_limits<FReal>::max();
					FReal CrossingHorizontalCellBorderT = std::numeric_limits<FReal>::max();
					if (!DxTooSmall)
					{
						CrossingVerticleCellBorderT = DtDx * ((FReal)(CurrentCellIndexX0 + (DeltaCelIndexX > 0 ? 1 : 0)) * DirtyElementGridCellSize - X1);
					}

					if (!DyTooSmall)
					{
						CrossingHorizontalCellBorderT = DtDy * ((FReal)(CurrentCellIndexY0 + (DeltaCelIndexY > 0 ? 1 : 0)) * DirtyElementGridCellSize - Y1);
					}

					if (CrossingHorizontalCellBorderT < CrossingVerticleCellBorderT)
					{
						CurrentCellIndexY1 += DeltaCelIndexY;
					}

					if (!DxTooSmall)
					{
						X1 += CrossingVerticleCellBorderT * (1 / DtDx);
					}

					if (!DyTooSmall)
					{
						Y1 += CrossingVerticleCellBorderT * (1 / DtDy);
					}
				}
				CurrentCellIndexX1 += DeltaCelIndexX;
			}
			Done = (CurrentCellIndexY0 == LastCellIndexY) && (DeltaCelIndexX * CurrentCellIndexX0 > LastCellIndexX * DeltaCelIndexX);
		}
	}

	template <typename FunctionType>
	FORCEINLINE_DEBUGGABLE void DoForSweepIntersectCells(const TVec3<FReal>& QueryHalfExtents, const TVector<FReal, 3>& StartPoint, const TVector<FReal, 3>& Dir, FReal Length, FReal DirtyElementGridCellSize, FReal DirtyElementGridCellSizeInv, FunctionType InFunction)
	{
		const TVector<FReal, 3>& EndPoint = StartPoint + Length * Dir;
		FReal DeltaX = EndPoint.X - StartPoint.X;
		FReal DeltaY = EndPoint.Y - StartPoint.Y;

		FReal AbsDx = FMath::Abs(DeltaX);
		FReal AbsDy = FMath::Abs(DeltaY);


		bool XDirectionDominant = AbsDx >= AbsDy;

		if (XDirectionDominant)
		{
			DoForSweepIntersectCellsImp(QueryHalfExtents.X, QueryHalfExtents.Y, StartPoint.X, StartPoint.Y, EndPoint.X, EndPoint.Y, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, InFunction);
		}
		else
		{
			// Swap Y and X
			DoForSweepIntersectCellsImp(QueryHalfExtents.Y, QueryHalfExtents.X, StartPoint.Y, StartPoint.X, EndPoint.Y, EndPoint.X, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](FReal X, FReal Y) {InFunction(Y, X); });
		}

	}

	FORCEINLINE_DEBUGGABLE bool TooManyRaycastQueryCells(const TVector<FReal, 3>& StartPoint, const TVector<FReal, 3>& Dir, const FReal Length, FReal DirtyElementGridCellSizeInv, int32 DirtyElementMaxGridCellQueryCount)
	{
		const TVector<FReal, 3>& EndPoint = StartPoint + Length * Dir;

		int32 FirstCellIndexX = (int32)FMath::Floor(StartPoint.X * DirtyElementGridCellSizeInv);
		int32 FirstCellIndexY = (int32)FMath::Floor(StartPoint.Y * DirtyElementGridCellSizeInv);

		int32 LastCellIndexX = (int32)FMath::Floor(EndPoint.X * DirtyElementGridCellSizeInv);
		int32 LastCellIndexY = (int32)FMath::Floor(EndPoint.Y * DirtyElementGridCellSizeInv);

		// This will be equal to the Manhattan distance 
		int CellCount = FMath::Abs(FirstCellIndexX - LastCellIndexX) + FMath::Abs(FirstCellIndexY - LastCellIndexY);

		if (CellCount > DirtyElementMaxGridCellQueryCount)
		{
			return true;
		}

		return false;

	}

	template <typename FunctionType>
	FORCEINLINE_DEBUGGABLE void DoForRaycastIntersectCells(const TVector<FReal, 3>& StartPoint, const TVector<FReal, 3>& Dir, FReal Length, FReal DirtyElementGridCellSize, FReal DirtyElementGridCellSizeInv, FunctionType InFunction)
	{
		const TVector<FReal, 3>& EndPoint = StartPoint + Length * Dir;

		int32 CurrentCellIndexX = (int32)FMath::Floor(StartPoint.X * DirtyElementGridCellSizeInv);
		int32 CurrentCellIndexY = (int32)FMath::Floor(StartPoint.Y * DirtyElementGridCellSizeInv);

		int32 LastCellIndexX = (int32)FMath::Floor(EndPoint.X * DirtyElementGridCellSizeInv);
		int32 LastCellIndexY = (int32)FMath::Floor(EndPoint.Y * DirtyElementGridCellSizeInv);

		FReal DeltaX = EndPoint.X - StartPoint.X;
		FReal DeltaY = EndPoint.Y - StartPoint.Y;

		FReal AbsDx = FMath::Abs(DeltaX);
		FReal AbsDy = FMath::Abs(DeltaY);

		bool DxTooSmall = AbsDx <= SMALL_NUMBER;
		bool DyTooSmall = AbsDy <= SMALL_NUMBER;

		if (DxTooSmall && DyTooSmall)
		{
			InFunction(HashCoordinates(StartPoint.X, StartPoint.Y, DirtyElementGridCellSizeInv));
			return;
		}

		int DeltaCelIndexX = DeltaX >= 0 ? 1 : -1;
		int DeltaCelIndexY = DeltaY >= 0 ? 1 : -1;

		FReal DtDy = 0; // DeltaTime over DeltaX
		FReal DtDx = 0;

		bool XDirectionDominant = AbsDx >= AbsDy;
		// Use parametric description of line here (t is the parameter and is positive along the ray)
		// x = Dx/Dt*t + x0
		// y = Dy/Dt*t + x0
		if (XDirectionDominant)
		{
			DtDx = (FReal)DeltaCelIndexX;
			DtDy = DyTooSmall ? 1 : (FReal)DeltaCelIndexX * DeltaX / DeltaY;
		}
		else
		{
			DtDx = DxTooSmall ? 1 : (FReal)DeltaCelIndexY * DeltaY / DeltaX;
			DtDy = (FReal)DeltaCelIndexY;
		}

		FReal X = StartPoint.X;
		FReal Y = StartPoint.Y;

		bool Done = false;
		while (!Done)
		{
			InFunction(HashCoordinates((FReal)CurrentCellIndexX * DirtyElementGridCellSize, (FReal)CurrentCellIndexY * DirtyElementGridCellSize, DirtyElementGridCellSizeInv));
			FReal CrossingVerticleCellBorderT = std::numeric_limits<FReal>::max();
			FReal CrossingHorizontalCellBorderT = std::numeric_limits<FReal>::max();
			if (!DxTooSmall)
			{
				CrossingVerticleCellBorderT = DtDx * ((FReal)(CurrentCellIndexX + (DeltaCelIndexX > 0 ? 1 : 0)) * DirtyElementGridCellSize - X);
			}

			if (!DyTooSmall)
			{
				CrossingHorizontalCellBorderT = DtDy * ((FReal)(CurrentCellIndexY + (DeltaCelIndexY > 0 ? 1 : 0)) * DirtyElementGridCellSize - Y);
			}

			FReal SmallestT;
			if (CrossingVerticleCellBorderT <= CrossingHorizontalCellBorderT)
			{
				CurrentCellIndexX += DeltaCelIndexX;
				SmallestT = CrossingVerticleCellBorderT;
			}
			else
			{
				CurrentCellIndexY += DeltaCelIndexY;
				SmallestT = CrossingHorizontalCellBorderT;
			}

			if (!DxTooSmall)
			{
				X += SmallestT * (1 / DtDx);
			}

			if (!DyTooSmall)
			{
				Y += SmallestT * (1 / DtDy);
			}

			if (DeltaCelIndexX * CurrentCellIndexX > DeltaCelIndexX * LastCellIndexX || DeltaCelIndexY * CurrentCellIndexY > DeltaCelIndexY * LastCellIndexY)
			{
				Done = true;
			}
		}
	}

}
