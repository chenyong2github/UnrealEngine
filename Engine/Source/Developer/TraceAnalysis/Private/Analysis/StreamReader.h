// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE {
namespace Trace {

////////////////////////////////////////////////////////////////////////////////
class FStreamReader
{
public:
								~FStreamReader();
	template <typename Type>
	Type const*					GetPointer();
	template <typename Type>
	Type const*					GetPointerUnchecked();
	const uint8*				GetPointer(uint32 Size);
	const uint8*				GetPointerUnchecked();
	void						Advance(uint32 Size);
	bool						IsEmpty() const;
	int32						GetRemaining() const;
	bool						Backtrack(const uint8* To);
	struct FMark*				SaveMark() const;
	void						RestoreMark(struct FMark* Mark);

protected:
	uint8*						Buffer = nullptr;
	uint32						DemandHint = 0;
	uint32						Cursor = 0;
	uint32						End = 0;
};

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
Type const* FStreamReader::GetPointer()
{
	return (Type const*)GetPointer(sizeof(Type));
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
Type const* FStreamReader::GetPointerUnchecked()
{
	return (Type const*)GetPointerUnchecked();
}

////////////////////////////////////////////////////////////////////////////////
inline const uint8* FStreamReader::GetPointerUnchecked()
{
	return Buffer + Cursor;
}



////////////////////////////////////////////////////////////////////////////////
class FStreamBuffer
	: public FStreamReader
{
public:
	template <typename Lambda>
	int32						Fill(Lambda&& Source);
	void						Append(const uint8* Data, uint32 Size);
	uint8*						Append(uint32 Size);

protected:
	void						Consolidate();
	uint32						BufferSize = 0;
};

////////////////////////////////////////////////////////////////////////////////
template <typename Lambda>
inline int32 FStreamBuffer::Fill(Lambda&& Source)
{
	Consolidate();

	uint8* Dest = Buffer + End;
	int32 ReadSize = Source(Dest, BufferSize - End);
	if (ReadSize > 0)
	{
		End += ReadSize;
	}

	return ReadSize;
}

} // namespace Trace
} // namespace UE
