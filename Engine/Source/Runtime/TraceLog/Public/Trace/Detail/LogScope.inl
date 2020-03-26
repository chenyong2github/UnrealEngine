// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "EventNode.h"
#include "LogScope.h"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
extern TRACELOG_API uint32 volatile	GLogSerial;

////////////////////////////////////////////////////////////////////////////////
inline FLogScope::~FLogScope()
{
	FWriteBuffer* Buffer = Instance.Buffer;
	AtomicStoreRelease<uint8* __restrict>(&(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
template <uint32 Flags>
inline FLogScope FLogScope::Enter(uint32 Uid, uint32 Size)
{
	FLogScope Ret;
	bool bMaybeHasAux = (Flags & FEventInfo::Flag_MaybeHasAux) != 0;
	if ((Flags & FEventInfo::Flag_NoSync) != 0)
	{
		Ret.EnterNoSync(Uid, Size, bMaybeHasAux);
	}
	else
	{
		Ret.Enter(Uid, Size, bMaybeHasAux);
	}
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
inline uint8* FLogScope::GetPointer() const
{
	return Instance.Ptr;
}

////////////////////////////////////////////////////////////////////////////////
inline const FLogScope& FLogScope::operator << (bool) const
{
	return *this;
}

////////////////////////////////////////////////////////////////////////////////
template <class HeaderType>
inline void FLogScope::EnterPrelude(uint32 Size, bool bMaybeHasAux)
{
	uint32 AllocSize = sizeof(HeaderType) + Size + int(bMaybeHasAux);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	Buffer->Cursor += AllocSize;
	if (UNLIKELY(Buffer->Cursor > (uint8*)Buffer))
	{
		Buffer = Writer_NextBuffer(AllocSize);
	}

	// The auxilary data null terminator.
	if (bMaybeHasAux)
	{
		Buffer->Cursor[-1] = 0;
	}

	uint8* Cursor = Buffer->Cursor - Size - int(bMaybeHasAux);
	Instance = {Cursor, Buffer};
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::Enter(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeaderSync>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr - sizeof(FEventHeaderSync::SerialHigh)); // FEventHeader1
	*(uint32*)(Header - 1) = uint32(AtomicIncrementRelaxed(&GLogSerial));
	Header[-2] = uint16(Size);
	Header[-3] = uint16(Uid)|int(EKnownEventUids::Flag_TwoByteUid);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::EnterNoSync(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeader>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr);
	Header[-1] = uint16(Size);
	Header[-2] = uint16(Uid)|int(EKnownEventUids::Flag_TwoByteUid);
}




////////////////////////////////////////////////////////////////////////////////
template <class T>
auto TLogScope<T>::Enter(uint32 Uid, uint32 Size)
{
	return TLogScopeSelector<T::bIsImportant>::Type::template Enter<T::EventFlags>(Uid, Size);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
auto TLogScope<T>::Enter(uint32 Uid, uint32 Size, uint32 ExtraBytes)
{
	return Enter(Uid, Size + ExtraBytes);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
