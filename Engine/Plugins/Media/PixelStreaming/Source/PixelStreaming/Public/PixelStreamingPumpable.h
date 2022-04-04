// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "rtc_base/ref_counter.h"
#include "rtc_base/ref_count.h"
#include "HAL/Platform.h"

/*
* Base class for objects that need to be pumped at a fixed interval.
*/
class PIXELSTREAMING_API FPixelStreamingPumpable
{
public:
	FPixelStreamingPumpable() = default;
	virtual ~FPixelStreamingPumpable() = default;
	virtual void OnPump(int32 FrameId) = 0;
	virtual bool IsReadyForPump() const = 0;
	virtual void AddRef() const { RefCount.IncRef(); }
	virtual bool HasOneRef() const { return RefCount.HasOneRef(); }
	virtual rtc::RefCountReleaseStatus Release() const
	{
		const rtc::RefCountReleaseStatus Status = RefCount.DecRef();
		if (Status == rtc::RefCountReleaseStatus::kDroppedLastRef)
		{
			delete this;
		}
		return Status;
	}

protected:
	mutable webrtc::webrtc_impl::RefCounter RefCount{ 0 };
};
