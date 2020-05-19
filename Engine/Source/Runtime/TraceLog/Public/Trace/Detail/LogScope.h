// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Writer.inl"

namespace Trace {
namespace Private {

struct FWriteBuffer;

////////////////////////////////////////////////////////////////////////////////
class FLogScope
{
public:
	constexpr explicit		operator bool () const { return true; }
	uint8*					GetPointer() const;
	void					Commit() const;
	const FLogScope&		operator << (bool) const;
	void					operator += (const FLogScope&) const;
	template <uint32 Flags>
	static FLogScope		Enter(uint32 Uid, uint32 Size);

private:
	template <class T> void	EnterPrelude(uint32 Size, bool bMaybeHasAux);
	void					Enter(uint32 Uid, uint32 Size, bool bMaybeHasAux);
	void					EnterNoSync(uint32 Uid, uint32 Size, bool bMaybeHasAux);
	struct
	{
		uint8*				Ptr;
		FWriteBuffer*		Buffer;
	}						Instance;
};

////////////////////////////////////////////////////////////////////////////////
class FImportantLogScope
	: public FLogScope
{
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



////////////////////////////////////////////////////////////////////////////////
template <bool>	struct TLogScopeSelector;
template <>		struct TLogScopeSelector<false>	{ typedef FLogScope Type; };
template <>		struct TLogScopeSelector<true>	{ typedef FImportantLogScope Type; };

////////////////////////////////////////////////////////////////////////////////
template <class T>
struct TLogScope
{
	static auto Enter(uint32 Uid, uint32 Size);
	static auto ScopedEnter(uint32 Uid, uint32 Size);
	static auto ScopedStampedEnter(uint32 Uid, uint32 Size);
};

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
