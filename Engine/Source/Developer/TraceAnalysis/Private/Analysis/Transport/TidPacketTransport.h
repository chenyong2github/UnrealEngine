// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Transport.h"
#include "Analysis/StreamReader.h"
#include "Containers/Array.h"

namespace Trace
{

class FStreamReader;

////////////////////////////////////////////////////////////////////////////////
class FTidPacketTransport
	: public FTransport
{
public:
	typedef UPTRINT ThreadIter;

	void					Update();
	uint32					GetThreadCount() const;
	FStreamReader*			GetThreadStream(uint32 Index);
	int32					GetThreadId(uint32 Index) const;

private:
	struct FThreadStream
	{
		FStreamBuffer		Buffer;
		uint32				ThreadId;
	};

	bool					ReadPacket();
	FThreadStream&			FindOrAddThread(uint32 ThreadId);
	TArray<FThreadStream>	Threads;
};

} // namespace Trace
