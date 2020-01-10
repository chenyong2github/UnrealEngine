// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

struct FTraceAnalyzerUtils
{
	static uint64 Decode7bit(const uint8*& BufferPtr)
	{
		uint64 Value = 0;
		uint64 ByteIndex = 0;
		bool HasMoreBytes;
		do
		{
			uint8 ByteValue = *BufferPtr++;
			HasMoreBytes = ByteValue & 0x80;
			Value |= uint64(ByteValue & 0x7f) << (ByteIndex * 7);
			++ByteIndex;
		} while (HasMoreBytes);
		return Value;
	}

	static int64 DecodeZigZag(const uint8*& BufferPtr)
	{
		uint64 Z = Decode7bit(BufferPtr);
		return (Z & 1) ? (Z >> 1) ^ -1 : (Z >> 1);
	}
};
