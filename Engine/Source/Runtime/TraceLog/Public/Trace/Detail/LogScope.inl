// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "EventNode.h"
#include "LogScope.h"
#include "Protocol.h"
#include "Writer.inl"

namespace UE {
namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
extern TRACELOG_API uint32 volatile	GLogSerial;

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::Commit() const
{
	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::operator += (const FLogScope&) const
{
	Commit();
}

////////////////////////////////////////////////////////////////////////////////
template <uint32 Flags>
inline FLogScope FLogScope::EnterImpl(uint32 Uid, uint32 Size)
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
template <class HeaderType>
inline void FLogScope::EnterPrelude(uint32 Size, bool bMaybeHasAux)
{
	uint32 AllocSize = sizeof(HeaderType) + Size + int32(bMaybeHasAux);

	Buffer = Writer_GetBuffer();
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

	Ptr = Buffer->Cursor - Size - int32(bMaybeHasAux);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::Enter(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeaderSync>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Ptr - sizeof(FEventHeaderSync::SerialHigh));
	*(uint32*)(Header - 1) = uint32(AtomicAddRelaxed(&GLogSerial, 1u));
	Header[-2] = uint16(Size);
	Header[-3] = uint16(Uid)|int32(EKnownEventUids::Flag_TwoByteUid);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::EnterNoSync(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeader>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Ptr);
	Header[-1] = uint16(Size);
	Header[-2] = uint16(Uid)|int32(EKnownEventUids::Flag_TwoByteUid);
}



////////////////////////////////////////////////////////////////////////////////
inline FScopedLogScope::~FScopedLogScope()
{
	if (!bActive)
	{
		return;
	}

	uint8 LeaveUid = uint8(EKnownEventUids::LeaveScope << EKnownEventUids::_UidShift);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor)) < int32(sizeof(LeaveUid)))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Buffer->Cursor[0] = LeaveUid;
	Buffer->Cursor += sizeof(LeaveUid);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FScopedLogScope::SetActive()
{
	bActive = true;
}



////////////////////////////////////////////////////////////////////////////////
inline FScopedStampedLogScope::~FScopedStampedLogScope()
{
	if (!bActive)
	{
		return;
	}

	FWriteBuffer* Buffer = Writer_GetBuffer();

	uint64 Stamp = Writer_GetTimestamp(Buffer);

	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor) < int32(sizeof(Stamp))))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Stamp <<= 8;
	Stamp += uint8(EKnownEventUids::LeaveScope_T << EKnownEventUids::_UidShift);
	memcpy((uint64*)(Buffer->Cursor), &Stamp, sizeof(Stamp));
	Buffer->Cursor += sizeof(Stamp);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FScopedStampedLogScope::SetActive()
{
	bActive = true;
}



////////////////////////////////////////////////////////////////////////////////
template <class T>
FORCENOINLINE FLogScope FLogScope::Enter(uint32 ExtraSize)
{
	uint32 Size = T::GetSize() + ExtraSize;
	uint32 Uid = T::GetUid();
	return EnterImpl<T::EventFlags>(Uid, Size);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
FORCENOINLINE FLogScope FLogScope::ScopedEnter(uint32 ExtraSize)
{
	uint8 EnterUid = uint8(EKnownEventUids::EnterScope << EKnownEventUids::_UidShift);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor)) < int32(sizeof(EnterUid)))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Buffer->Cursor[0] = EnterUid;
	Buffer->Cursor += sizeof(EnterUid);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);

	return Enter<T>(ExtraSize);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
FORCENOINLINE FLogScope FLogScope::ScopedStampedEnter(uint32 ExtraSize)
{
	uint64 Stamp;

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor) < int32(sizeof(Stamp))))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Stamp = Writer_GetTimestamp(Buffer);
	Stamp <<= 8;
	Stamp += uint8(EKnownEventUids::EnterScope_T << EKnownEventUids::_UidShift);
	memcpy((uint64*)(Buffer->Cursor), &Stamp, sizeof(Stamp));
	Buffer->Cursor += sizeof(Stamp);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);

	return Enter<T>(ExtraSize);
}



////////////////////////////////////////////////////////////////////////////////
template <typename FieldMeta, typename Type>
struct FLogScope::FFieldSet
{
	static void Impl(FLogScope* Scope, const Type& Value)
	{
		uint8* Dest = (uint8*)(Scope->Ptr) + FieldMeta::Offset;
		::memcpy(Dest, &Value, sizeof(Type));
	}
};

////////////////////////////////////////////////////////////////////////////////
template <typename FieldMeta, typename Type>
struct FLogScope::FFieldSet<FieldMeta, Type[]>
{
	static void Impl(FLogScope*, Type const* Data, int32 Num)
	{
		static const uint32 Index = FieldMeta::Index & 0x7f;
		int32 Size = (Num * sizeof(Type)) & (FAuxHeader::SizeLimit - 1) & ~(sizeof(Type) - 1);
		Field_WriteAuxData(Index, (const uint8*)Data, Size);
	}
};

#if STATICALLY_SIZED_ARRAY_FIELDS_SUPPORT
////////////////////////////////////////////////////////////////////////////////
template <typename FieldMeta, typename Type, int32 Count>
struct FLogScope::FFieldSet<FieldMeta, Type[Count]>
{
	static void Impl(FLogScope*, Type const* Data, int32 Num=-1) = delete;
};
#endif // STATICALLY_SIZED_ARRAY_FIELDS_SUPPORT

////////////////////////////////////////////////////////////////////////////////
template <typename FieldMeta>
struct FLogScope::FFieldSet<FieldMeta, AnsiString>
{
	static void Impl(FLogScope*, const ANSICHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = int32(strlen(String));
		}

		static const uint32 Index = FieldMeta::Index & 0x7f;
		Field_WriteStringAnsi(Index, String, Length);
	}

	static void Impl(FLogScope*, const TCHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = 0;
			for (const TCHAR* c = String; *c; ++c, ++Length);
		}

		static const uint32 Index = FieldMeta::Index & 0x7f;
		Field_WriteStringAnsi(Index, String, Length);
	}
};

////////////////////////////////////////////////////////////////////////////////
template <typename FieldMeta>
struct FLogScope::FFieldSet<FieldMeta, WideString>
{
	static void Impl(FLogScope*, const TCHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = 0;
			for (const TCHAR* c = String; *c; ++c, ++Length);
		}

		static const uint32 Index = FieldMeta::Index & 0x7f;
		Field_WriteStringWide(Index, String, Length);
	}
};

template <typename FieldMeta>
struct FLogScope::FFieldSet<FieldMeta, Attachment>
{
	template <typename LambdaType>
	static void Impl(FLogScope* Scope, LambdaType&& Lambda)
	{
		uint8* Dest = (uint8*)(Scope->Ptr) + FieldMeta::Offset;
		Lambda(Dest);
	}

	static void Impl(FLogScope* Scope, const void* Data, uint32 Size)
	{
		uint8* Dest = (uint8*)(Scope->Ptr) + FieldMeta::Offset;
		::memcpy(Dest, Data, Size);
	}
};

} // namespace Private
} // namespace Trace
} // namespace UE

#endif // UE_TRACE_ENABLED
