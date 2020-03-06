// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channel.h"

#if UE_TRACE_ENABLED

namespace Trace {

////////////////////////////////////////////////////////////////////////////////
inline bool FChannel::IsEnabled() const
{
	return !bDisabled;
}

////////////////////////////////////////////////////////////////////////////////

inline FChannel::operator bool() const
{
	return IsEnabled();
}

////////////////////////////////////////////////////////////////////////////////
inline bool FChannel::operator|(const FChannel& Rhs) const
{
	return IsEnabled() && Rhs.IsEnabled();
}


////////////////////////////////////////////////////////////////////////////////
struct FTraceChannel : public FChannel
{
	bool IsEnabled() const { return true; }
	explicit operator bool() const { return true; }
};

}

extern TRACELOG_API Trace::FTraceChannel TraceLogChannel;

#endif //UE_TRACE_ENABLED