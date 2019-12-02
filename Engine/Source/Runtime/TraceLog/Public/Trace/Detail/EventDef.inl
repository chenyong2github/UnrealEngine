// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "EventDef.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size)
{
	Instance = Writer_BeginLog(EventUid, Size);
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size, uint16 ExtraBytes)
{
	Instance = Writer_BeginLog(EventUid, Size + ExtraBytes);
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
