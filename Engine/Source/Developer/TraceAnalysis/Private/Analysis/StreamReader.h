// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace Trace
{

class IInDataStream;

////////////////////////////////////////////////////////////////////////////////
class FStreamReader
{
public:
	const uint8*		GetPointer(uint32 Size) const;
	void				Advance(uint32 Size);

protected:
	const uint8*		Cursor = nullptr;
	const uint8*		End = nullptr;
	mutable uint32		LastRequestedSize = 0;
};

////////////////////////////////////////////////////////////////////////////////
class FStreamReaderDetail
	: public FStreamReader
{
public:
						FStreamReaderDetail(IInDataStream& InDataStream);
						~FStreamReaderDetail();
	bool				Read();

private:
	IInDataStream&		DataStream;
	uint8*				Buffer;
	uint32				BufferSize;
};

} // namespace Trace
