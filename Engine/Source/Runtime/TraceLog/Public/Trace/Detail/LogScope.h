// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Writer.inl"

namespace UE {
namespace Trace {
namespace Private {

struct FWriteBuffer;

////////////////////////////////////////////////////////////////////////////////
class FLogScope
{
public:
	template <typename EventType>
	static FLogScope		Enter(uint32 ExtraSize=0);
	template <typename EventType>
	static FLogScope		ScopedEnter(uint32 ExtraSize=0);
	template <typename EventType>
	static FLogScope		ScopedStampedEnter(uint32 ExtraSize=0);
	void*					GetPointer() const			{ return Ptr; }
	void					Commit() const;
	void					operator += (const FLogScope&) const;
	const FLogScope&		operator << (bool) const	{ return *this; }
	constexpr explicit		operator bool () const		{ return true; }

	template <typename FieldMeta, typename Type>
	struct FFieldSet;

private:
	template <uint32 Flags>
	static FLogScope		EnterImpl(uint32 Uid, uint32 Size);
	template <class T> void	EnterPrelude(uint32 Size, bool bMaybeHasAux);
	void					Enter(uint32 Uid, uint32 Size, bool bMaybeHasAux);
	void					EnterNoSync(uint32 Uid, uint32 Size, bool bMaybeHasAux);
	uint8*					Ptr;
	FWriteBuffer*			Buffer;
};



////////////////////////////////////////////////////////////////////////////////
class FScopedLogScope
{
public:
			~FScopedLogScope();
	void	SetActive();
	bool	bActive = false;
};

////////////////////////////////////////////////////////////////////////////////
class FScopedStampedLogScope
{
public:
			~FScopedStampedLogScope();
	void	SetActive();
	bool	bActive = false;
};

} // namespace Private
} // namespace Trace
} // namespace UE

#endif // UE_TRACE_ENABLED
