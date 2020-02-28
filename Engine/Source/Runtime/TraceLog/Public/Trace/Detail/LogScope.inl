// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "EventNode.h"
#include "LogScope.h"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
inline FLogScope::~FLogScope()
{
	Writer_EndLog(Instance);
}

////////////////////////////////////////////////////////////////////////////////
template <uint32 Flags>
inline FLogScope FLogScope::Enter(uint32 Uid, uint32 Size)
{
	FLogScope Ret;
	bool bMaybeHasAux = (Flags & FEventInfo::Flag_MaybeHasAux) != 0;
	Ret.Instance = (Flags & FEventInfo::Flag_NoSync)
		?  Writer_BeginLogNoSync(uint16(Uid), uint16(Size), bMaybeHasAux)
		:  Writer_BeginLog(uint16(Uid), uint16(Size), bMaybeHasAux);
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
inline uint8* FLogScope::GetPointer() const
{
	return Instance.Ptr;
}



////////////////////////////////////////////////////////////////////////////////
template <class T>
auto TLogScope<T& __restrict>::Enter(uint32 Uid, uint32 Size)
{
	return TLogScopeSelector<T::bIsImportant>::Type::template Enter<T::EventFlags>(Uid, Size);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
auto TLogScope<T& __restrict>::Enter(uint32 Uid, uint32 Size, uint32 ExtraBytes)
{
	return Enter(Uid, Size + ExtraBytes);
}



////////////////////////////////////////////////////////////////////////////////
template <typename ActionType>
inline const FLogScope& FLogScope::operator << (const ActionType& Rhs) const
{
	Rhs.Write(Instance.Ptr);
	return *this;
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
