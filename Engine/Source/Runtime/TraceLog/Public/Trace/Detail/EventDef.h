// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Writer.inl"

namespace Trace
{

struct FFieldDesc;
struct FLiteralName;

////////////////////////////////////////////////////////////////////////////////
class FEventDef
{
public:
	enum
	{
		Flag_Important		= 1 << 0,
		Flag_MaybeHasAux	= 1 << 1,
		Flag_NoSync			= 1 << 3,
	};

	class FLogScope
	{
	public:
								FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags);
								FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags, uint16 ExtraBytes);
								~FLogScope();
		FLogInstance			Instance;
		constexpr explicit		operator bool () const { return true; }
	};

	uint16						Uid;
	bool						bInitialized;
	bool						bImportant;
	TRACELOG_API static void	Create(FEventDef* Target, const FLiteralName& LoggerName, const FLiteralName& EventName, const FFieldDesc* FieldDescs, uint32 FieldCount, uint32 Flags=0);
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
