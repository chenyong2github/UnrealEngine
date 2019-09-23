// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/LoadTimeProfiler.h"

class FTimingEventsTrack;

struct FTimingEvent
{
	static const uint64 InvalidTypeId = uint64(-1);

	const FTimingEventsTrack* Track;
	uint64 TypeId;
	uint32 Depth;
	double StartTime;
	double EndTime;
	double ExclusiveTime;

	// For Asset Loading events...
	Trace::FLoadTimeProfilerCpuEvent LoadingInfo;

	// For I/O events...
	uint64 Offset;
	uint64 Size;
	const TCHAR* Path;

	FTimingEvent()
		: Track(nullptr)
		, TypeId(InvalidTypeId)
		, Depth(0)
		, StartTime(0.0)
		, EndTime(0.0)
		, ExclusiveTime(0.0)
		, LoadingInfo()
		, Offset(0)
		, Size(0)
		, Path(nullptr)
	{
	}

	FTimingEvent(const FTimingEvent&) = default;
	FTimingEvent& operator=(const FTimingEvent&) = default;

	FTimingEvent(FTimingEvent&&) = default;
	FTimingEvent& operator=(FTimingEvent&&) = default;

	void Reset()
	{
		Track = nullptr;
		TypeId = InvalidTypeId;
		Depth = 0;
		StartTime = 0.0;
		EndTime = 0.0;
		ExclusiveTime = 0.0;

		LoadingInfo.Package = nullptr;
		LoadingInfo.Export = nullptr;
		LoadingInfo.PackageEventType = LoadTimeProfilerPackageEventType_None;
		LoadingInfo.ExportEventType = LoadTimeProfilerObjectEventType_None;

		Offset = 0;
		Size = 0;
		Path = nullptr;
	}

	bool IsValidTrack() const { return Track != nullptr; }

	bool IsValid() const { return Track != nullptr && TypeId != InvalidTypeId; }

	double Duration() const { return EndTime - StartTime; }

	bool Equals(const FTimingEvent& Other) const
	{
		return Track == Other.Track
			&& TypeId == Other.TypeId
			&& Depth == Other.Depth
			&& StartTime == Other.StartTime
			&& EndTime == Other.EndTime;
	}

	static bool AreEquals(const FTimingEvent& A, const FTimingEvent& B)
	{
		return A.Equals(B);
	}
};
