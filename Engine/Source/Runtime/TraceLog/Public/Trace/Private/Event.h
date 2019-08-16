// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace.h"

namespace Trace
{

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4200) // non-standard zero-sized array
#endif

////////////////////////////////////////////////////////////////////////////////
struct FNewEventEvent
{
	enum : uint16 { Uid = 0 };

	uint16		EventUid;
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

#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

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

	enum
	{
		Flag_Always		= 1 << 0,
		Flag_Important	= 1 << 1,
	};

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
	union
	{
		struct
		{
			bool				bOptedIn;
			uint8				Internal;
		};
		uint16					Test;
	}							Enabled;
	bool						bInitialized;
	UE_TRACE_API static void	Create(FEvent* Target, const FLiteralName& LoggerName, const FLiteralName& EventName, const FFieldDesc* FieldDescs, uint32 FieldCount, uint32 Flags=0);
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
