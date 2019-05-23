// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
enum class EKnownEventUids : uint16
{
	NewEvent,
	User,
	Max			= 1 << 14, // ...leaves two MSB bits for other uses.
};

////////////////////////////////////////////////////////////////////////////////
struct FNewEventEvent
{
	uint16		Uid;
	uint16		FieldCount;
	uint8		LoggerNameSize;
	uint8		EventNameSize;
	struct
	{
		uint16	Offset;
		uint16	Size;
		uint8	TypeInfo;
		uint8	NameSize;
	}			Fields[];
	/*uint8		NameData[]*/
};

} // namespace Trace



#if UE_TRACE_ENABLED

namespace Trace
{

struct FFieldDesc;
struct FLiteralName;

////////////////////////////////////////////////////////////////////////////////
class FEvent
{
public:
	enum { HeaderSize = sizeof(uint32), }; // I'll just dump this here for now.

	class FLogScope
	{
	public:
						FLogScope(uint16 EventUid, uint16 Size);
						FLogScope(uint16 EventUid, uint16 Size, uint16 ExtraBytes);
						FLogScope(uint16 EventUid, uint16 Size, uint8* Out);
						~FLogScope();
		uint8*			Ptr;
		const bool		bOutOfBand = false;
	};

	void*						Handle;
	uint32						LoggerHash;
	uint32						Hash;
	uint16						Uid;
	bool						bInitialized;
	bool						bEnabled;
	UE_TRACE_API static void	Create(FEvent* Target, const FLiteralName& LoggerName, const FLiteralName& EventName, const FFieldDesc* FieldDescs, uint32 FieldCount);
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
