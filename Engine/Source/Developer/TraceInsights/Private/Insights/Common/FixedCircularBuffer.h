// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

template <typename T, int Size = 64>
struct TFixedCircularBuffer
{
	T Buffer[Size];
	int BufferStartOffset = 0;
	int BufferLength = 0;

	void Reset()
	{
		BufferStartOffset = 0;
		BufferLength = 0;
	}

	void AddValue(const T Value)
	{
		Buffer[BufferStartOffset] = Value;
		BufferStartOffset++;
		if (BufferStartOffset == Size)
			BufferStartOffset = 0;
		if (BufferLength < Size)
			BufferLength++;
	}

	const T GetValue(const int RecentIndex) const
	{
		return Buffer[(BufferStartOffset + Size - RecentIndex - 1) % Size];
	}

	const T ComputeAverage() const
	{
		if (BufferLength == 0)
			return 0;
		T Total = 0;
		for (int RecentIndex = 0; RecentIndex < BufferLength; ++RecentIndex)
			Total += Buffer[(BufferStartOffset + Size - RecentIndex - 1) % Size];
		return Total / BufferLength;
	}
};
