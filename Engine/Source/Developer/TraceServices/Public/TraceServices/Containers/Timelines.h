// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

namespace Trace
{

template<typename InEventType>
class ITimeline
{
public:
	typedef InEventType EventType;

	virtual ~ITimeline() = default;
	virtual uint64 GetModCount() const = 0;
	virtual uint64 GetEventCount() const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
};

}