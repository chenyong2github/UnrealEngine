// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBaseTimingTrack;

class TRACEINSIGHTS_API ITimingEvent
{
public:
	virtual const FName& GetTypeName() const = 0;

	virtual const TSharedRef<const FBaseTimingTrack> GetTrack() const = 0;

	bool CheckTrack(const FBaseTimingTrack* TrackPtr) const { return &GetTrack().Get() == TrackPtr; }

	virtual uint32 GetDepth() const = 0;

	virtual double GetStartTime() const = 0;
	virtual double GetEndTime() const = 0;
	virtual double GetDuration() const = 0;

	virtual bool Equals(const ITimingEvent& Other) const = 0;

	static bool AreEquals(const ITimingEvent& A, const ITimingEvent& B)
	{
		return A.Equals(B);
	}

	static bool AreValidAndEquals(const TSharedPtr<const ITimingEvent> A, const TSharedPtr<const ITimingEvent> B)
	{
		return A.IsValid() && B.IsValid() && (*A).Equals(*B);
	}
};

class TRACEINSIGHTS_API ITimingEventFilter
{
public:
	virtual const FName& GetTypeName() const = 0;

	virtual bool FilterTrack(const FBaseTimingTrack& InTrack) const = 0;
	virtual bool FilterEvent(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName = nullptr, uint64 InEventType = 0, uint32 InEventColor = 0) const = 0;

	virtual uint32 GetChangeNumber() const = 0;
};
