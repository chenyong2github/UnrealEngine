// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionCheck.h"

// Sets index to value, resizing bit array if necessary and setting new bits to false
template<typename BitArrayType>
void NpResizeAndSetBit(BitArrayType& BitArray, int32 Index, bool Value=true)
{
	if (!BitArray.IsValidIndex(Index))
	{
		const int32 PreNum = BitArray.Num();
		BitArray.SetNumUninitialized(Index+1);
		BitArray.SetRange(PreNum, BitArray.Num() - PreNum, false);
		npCheckSlow(BitArray.IsValidIndex(Index));
	}

	BitArray[Index] = Value;
}

// Resize BitArray to NewNum, setting default value of new bits to false
template<typename BitArrayType>
void NpResizeBitArray(BitArrayType& BitArray, int32 NewNum)
{
	if (BitArray.Num() < NewNum)
	{
		const int32 PreNum = BitArray.Num();
		BitArray.SetNumUninitialized(NewNum);
		BitArray.SetRange(PreNum, BitArray.Num() - PreNum, false);
		npCheckSlow(BitArray.Num() == NewNum);
	}
}

// Set bit array contents to false
template<typename BitArrayType>
void NpClearBitArray(BitArrayType& BitArray)
{
	BitArray.SetRange(0, BitArray.Num(), false);
}