// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


struct FEncoderPoint;


namespace LensInterpolationUtils
{
	//Flags for the possible direction on both axis
	enum class EDirectionFlags : uint8
	{
		None = 0 << 0,
		Negative = 1 << 0,
		Positive = 1 << 1,
	};
	ENUM_CLASS_FLAGS(EDirectionFlags);

	/** Helper struct when looking for four points around desired coord*/
	struct FGridPointInfo
	{
		FGridPointInfo(EDirectionFlags InFocusDirection, EDirectionFlags InZoomDirection)
		: NeededFocusDirection(InFocusDirection)
		, NeededZoomDirection(InZoomDirection)
		{}

		const EDirectionFlags NeededFocusDirection;
		const EDirectionFlags NeededZoomDirection;

		//Score given to this coord based on direction that respects the desired coord.
		//0: No direction respected
		//1: One direction respected
		//2:Two directions respected
		int32 DirectionScore = 0;

		//Index of that point in source data
		int32 Index = INDEX_NONE;

		//Cached value of that point coord (X = Focus, Y = Zoom)
		float Focus = MAX_FLT;
		float Zoom = MAX_FLT;
	};

	void BilinearInterpolate(const UStruct* InStruct
		, float MainCoefficient
		, float DeltaMinFocus
		, float DeltaMaxFocus
		, float DeltaMinZoom
		, float DeltaMaxZoom
		, const void* DataA, const void* DataB, const void* DataC, const void* DataD, void* OutFrameData);

	void Interpolate(const UStruct* InStruct, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);
	
	float GetBlendFactor(float InValue, float ValueA, float ValueB);
	bool InterpolateEncoderValue(float InNormalizedValue, TArrayView<FEncoderPoint> InSourceData, float& OutEvaluatedValue);

	/** Helper function to find the possibly four indices for the points around the desired coordinates */
	template<typename Type>
	bool FindBilinearInterpIndices(float Focus, float Zoom, TArrayView<Type> InSourceData, int32& OutPointIndexA, int32& OutPointIndexB, int32& OutPointIndexC, int32& OutPointIndexD)
	{
		if (InSourceData.Num() <= 0)
		{
			return false;
		}

		if (InSourceData.Num() == 1)
		{
			OutPointIndexA = OutPointIndexB = OutPointIndexC = OutPointIndexD = 0;
			return true;
		}

		//To keep track of the four points we found
		FGridPointInfo MinFocusMinZoom(EDirectionFlags::Negative, EDirectionFlags::Negative);
		FGridPointInfo MinFocusMaxZoom(EDirectionFlags::Negative, EDirectionFlags::Positive);
		FGridPointInfo MaxFocusMinZoom(EDirectionFlags::Positive, EDirectionFlags::Negative);
		FGridPointInfo MaxFocusMaxZoom(EDirectionFlags::Positive, EDirectionFlags::Positive);

		for (int32 Index = 0; Index < InSourceData.Num(); ++Index)
		{
			const Type& CurrentPoint = InSourceData[Index];

			UpdatePointIfBetter(Focus, Zoom, CurrentPoint, Index, MinFocusMinZoom);
			UpdatePointIfBetter(Focus, Zoom, CurrentPoint, Index, MinFocusMaxZoom);
			UpdatePointIfBetter(Focus, Zoom, CurrentPoint, Index, MaxFocusMinZoom);
			UpdatePointIfBetter(Focus, Zoom, CurrentPoint, Index, MaxFocusMaxZoom);
		}

		//Patch to fix bad point selection in corner case
		//For some cases, the optimal solution is a line going across the desired coord.
		//In its current form, the algorithm ends up selecting two identical points
		//plus another one across it but the fourth one is in diagonal
		//This will enforce that the point giving us a straight line is selected
		if (((MinFocusMinZoom.Index == MinFocusMaxZoom.Index) && (MaxFocusMinZoom.Index != MaxFocusMaxZoom.Index)))
		{
			const Type& DuplicatePoint = InSourceData[MinFocusMinZoom.Index];
			const Type& OtherPoint1 = InSourceData[MaxFocusMinZoom.Index];
			const Type& OtherPoint2 = InSourceData[MaxFocusMaxZoom.Index];
			const float DeltaFocus1 = FMath::Abs(OtherPoint1.Focus - DuplicatePoint.Focus);
			const float DeltaFocus2 = FMath::Abs(OtherPoint2.Focus - DuplicatePoint.Focus);
			const float DeltaZoom1 = FMath::Abs(OtherPoint1.Zoom - DuplicatePoint.Zoom);
			const float DeltaZoom2 = FMath::Abs(OtherPoint2.Zoom - DuplicatePoint.Zoom);
			if ((FMath::IsNearlyZero(DeltaFocus1) && !FMath::IsNearlyZero(DeltaZoom1))
				||
				(FMath::IsNearlyZero(DeltaZoom1) && !FMath::IsNearlyZero(DeltaFocus1)))
			{
				MaxFocusMaxZoom.Index = MaxFocusMinZoom.Index;
			}
			else if ((FMath::IsNearlyZero(DeltaFocus2) && !FMath::IsNearlyZero(DeltaZoom2))
				||
				(FMath::IsNearlyZero(DeltaZoom2) && !FMath::IsNearlyZero(DeltaFocus2)))
			{
				MaxFocusMinZoom.Index = MaxFocusMaxZoom.Index;
			}
		}
		else if ((MinFocusMinZoom.Index == MaxFocusMinZoom.Index) && (MinFocusMaxZoom.Index != MaxFocusMaxZoom.Index))
		{
			//One point is badly selected. Maximize the line here
			const Type& DuplicatePoint = InSourceData[MinFocusMinZoom.Index];
			const Type& OtherPoint1 = InSourceData[MinFocusMaxZoom.Index];
			const Type& OtherPoint2 = InSourceData[MaxFocusMaxZoom.Index];
			const float DeltaFocus1 = FMath::Abs(OtherPoint1.Focus - DuplicatePoint.Focus);
			const float DeltaFocus2 = FMath::Abs(OtherPoint2.Focus - DuplicatePoint.Focus);
			const float DeltaZoom1 = FMath::Abs(OtherPoint1.Zoom - DuplicatePoint.Zoom);
			const float DeltaZoom2 = FMath::Abs(OtherPoint2.Zoom - DuplicatePoint.Zoom);
			if ((FMath::IsNearlyZero(DeltaFocus1) && !FMath::IsNearlyZero(DeltaZoom1))
				||
				(FMath::IsNearlyZero(DeltaZoom1) && !FMath::IsNearlyZero(DeltaFocus1)))
			{
				MaxFocusMaxZoom.Index = MinFocusMaxZoom.Index;
			}
			else if ((FMath::IsNearlyZero(DeltaFocus2) && !FMath::IsNearlyZero(DeltaZoom2))
				||
				(FMath::IsNearlyZero(DeltaZoom2) && !FMath::IsNearlyZero(DeltaFocus2)))
			{
				MinFocusMaxZoom.Index = MaxFocusMaxZoom.Index;
			}
		}

		OutPointIndexA = MinFocusMinZoom.Index;
		OutPointIndexB = MinFocusMaxZoom.Index;
		OutPointIndexC = MaxFocusMinZoom.Index;
		OutPointIndexD = MaxFocusMaxZoom.Index;
		return true;
	}

	template<typename Type>
	void UpdatePointIfBetter(float Focus, float Zoom, const Type& NewPoint, int32 NewPointIndex, FGridPointInfo& CurrentPoint)
	{
		//Point is lower then desired coord so update MinMin if distance is smaller
		const float NewDistance = FMath::Pow(FMath::Abs(Focus - NewPoint.Focus), 2) + FMath::Pow(FMath::Abs(Zoom - NewPoint.Zoom), 2);
		const float CurrentDistance = FMath::Pow(FMath::Abs(Focus - CurrentPoint.Focus), 2) + FMath::Pow(FMath::Abs(Zoom - CurrentPoint.Zoom), 2);
		const float NewFocusDir = NewPoint.Focus - Focus;
		const float NewZoomDir = NewPoint.Zoom - Zoom;

		const EDirectionFlags NewPointFocusDirectionFlags = ((NewFocusDir >= 0.0f) ? EDirectionFlags::Positive : EDirectionFlags::None) 
													 | ((NewFocusDir <= 0.0f) ? EDirectionFlags::Negative : EDirectionFlags::None);
		const EDirectionFlags NewPointZoomDirectionFlags = ((NewZoomDir >= 0.0f) ? EDirectionFlags::Positive : EDirectionFlags::None)
													 | ((NewZoomDir <= 0.0f) ? EDirectionFlags::Negative : EDirectionFlags::None);

		const int32 NewDirectionScore = (EnumHasAnyFlags(CurrentPoint.NeededFocusDirection, NewPointFocusDirectionFlags) ? 1 : 0)
										+
										(EnumHasAnyFlags(CurrentPoint.NeededZoomDirection, NewPointZoomDirectionFlags) ? 1 : 0);

		bool bIsBetter = false;

		//If new points improve required direction, take it
		if (NewDirectionScore > CurrentPoint.DirectionScore)
		{
			bIsBetter = true;
		}
		else if(NewDirectionScore == CurrentPoint.DirectionScore)
		{
			//If new point has same score, favor closest one
			bIsBetter = NewDistance < CurrentDistance;
		}

		if (bIsBetter)
		{
			CurrentPoint.DirectionScore = NewDirectionScore;
			CurrentPoint.Index = NewPointIndex;
			CurrentPoint.Focus = NewPoint.Focus;
			CurrentPoint.Zoom = NewPoint.Zoom;
		}
	}

	template<typename Type>
	bool FIZMappingBilinearInterpolation(float InFocus, float InZoom, TArrayView<Type> InSourceData, Type& OutInterpolatedData)
	{
		int32 MinMinIndex = 0;
		int32 MinMaxIndex = 0;
		int32 MaxMinIndex = 0;
		int32 MaxMaxIndex = 0;

		//Start by finding the 4 points around the desired coords to do bilinear interpolation
		//Depending on data map and desired coordinates, we might end up 
		//  a. Not interpolating and returning a single entry point
		//  b. Linear interpolate between two points
		//  c. Bilinear interpolate between 4 points
		const bool bFoundIndices = FindBilinearInterpIndices(InFocus, InZoom, InSourceData, MinMinIndex, MinMaxIndex, MaxMinIndex, MaxMaxIndex);
		if (bFoundIndices)
		{
			check(InSourceData.IsValidIndex(MinMinIndex));
			check(InSourceData.IsValidIndex(MinMaxIndex));
			check(InSourceData.IsValidIndex(MaxMinIndex));
			check(InSourceData.IsValidIndex(MaxMaxIndex));

			const Type& MinMinPoint = InSourceData[MinMinIndex];
			const Type& MinMaxPoint = InSourceData[MinMaxIndex];
			const Type& MaxMinPoint = InSourceData[MaxMinIndex];
			const Type& MaxMaxPoint = InSourceData[MaxMaxIndex];

			//Single point case
			if (MinMinIndex == MaxMinIndex && MaxMinIndex == MinMaxIndex && MinMaxIndex == MaxMaxIndex)
			{
				OutInterpolatedData = InSourceData[MinMinIndex];
				return true;
			}
			else if (MinMinIndex == MaxMinIndex && MinMaxIndex == MaxMaxIndex)
			{
				//Fixed focus
				const float BlendingFactor = GetBlendFactor(InZoom, MinMinPoint.Zoom, MaxMaxPoint.Zoom);
				Interpolate(Type::StaticStruct(), BlendingFactor, &MinMinPoint, &MaxMaxPoint, &OutInterpolatedData);
				return true;
			}
			else if (MinMinIndex == MinMaxIndex && MaxMinIndex == MaxMaxIndex)
			{
				//Fixed zoom
				const float BlendingFactor = GetBlendFactor(InFocus, MinMinPoint.Focus, MaxMaxPoint.Focus);
				Interpolate(Type::StaticStruct(), BlendingFactor, &MinMinPoint, &MaxMaxPoint, &OutInterpolatedData);
				return true;
			}
			else
			{
				//The current grid finder doesn't always yield points around the sample
				const float X2X1 = MaxMinPoint.Focus - MinMinPoint.Focus;
				const float Y2Y1 = MaxMaxPoint.Zoom - MinMinPoint.Zoom;
				const float Divider = X2X1 * Y2Y1;

				if (!FMath::IsNearlyZero(Divider))
				{
					const float DeltaMaxFocus = MaxMinPoint.Focus - InFocus;
					const float DeltaMaxZoom = MaxMaxPoint.Zoom - InZoom;
					const float DeltaMinFocus = InFocus - MinMinPoint.Focus;
					const float DeltaMinZoom = InZoom - MinMinPoint.Zoom;
					const float MainCoeff = 1.0f / (X2X1 * Y2Y1);
					BilinearInterpolate(Type::StaticStruct(), MainCoeff, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, &MinMinPoint, &MinMaxPoint, &MaxMinPoint, &MaxMaxPoint, &OutInterpolatedData);
					return true;
				}
			}
		}

		return false;
	}

};
