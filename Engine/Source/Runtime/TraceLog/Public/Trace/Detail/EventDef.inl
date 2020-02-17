// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "EventDef.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags)
{
	const bool bMaybeHasAux = EventFlags & FEventDef::Flag_MaybeHasAux;
	Instance = (EventFlags & FEventDef::Flag_NoSync)
		? Writer_BeginLogNoSync(EventUid, Size, bMaybeHasAux)
		: Writer_BeginLog(EventUid, Size, bMaybeHasAux);
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size, uint32 EventFlags, uint16 ExtraBytes)
: FLogScope(EventUid, Size + ExtraBytes, EventFlags)
{
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::~FLogScope()
{
	Writer_EndLog(Instance);
}



////////////////////////////////////////////////////////////////////////////////
template <typename ActionType>
inline const FEventDef::FLogScope& operator << (const FEventDef::FLogScope& Lhs, const ActionType& Rhs)
{
	Rhs.Write(Lhs.Instance.Ptr);
	return Lhs;
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
