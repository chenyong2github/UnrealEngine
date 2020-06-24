// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Writer.inl"

namespace Trace {

////////////////////////////////////////////////////////////////////////////////
class FEventDef
{
public:
	class FLogScope
	{
	public:
								FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags);
								FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags, uint16 ExtraBytes);
								~FLogScope();
		FLogInstance			Instance;
		constexpr explicit		operator bool () const { return true; }
	};
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
