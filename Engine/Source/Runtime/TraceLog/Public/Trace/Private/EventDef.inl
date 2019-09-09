// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "EventDef.h"
#include "Writer.inl"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size)
{
	Ptr = Writer_BeginLog(EventUid, Size);
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size, uint16 ExtraBytes)
{
	Ptr = Writer_BeginLog(EventUid, Size + ExtraBytes);
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::FLogScope(uint16 EventUid, uint16 Size, uint8* Out)
: bOutOfBand(true)
{
	*(uint32*)Out = (uint32(Size) << 16)|uint32(EventUid);
	Ptr = Out + HeaderSize;
}

////////////////////////////////////////////////////////////////////////////////
inline FEventDef::FLogScope::~FLogScope()
{
	if (!bOutOfBand)
	{
		Writer_EndLog(Ptr);
	}
}



////////////////////////////////////////////////////////////////////////////////
template <typename ActionType>
inline const FEventDef::FLogScope& operator << (const FEventDef::FLogScope& Lhs, const ActionType& Rhs)
{
	Rhs.Write(Lhs.Ptr);
	return Lhs;
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
