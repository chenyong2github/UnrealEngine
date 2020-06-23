// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Insights
{

typedef int32 FMemoryTrackerId;
class FMemoryTag;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryTracker
{
	friend class FMemorySharedState;

public:
	static const FMemoryTrackerId InvalidTrackerId = -1;

public:
	FMemoryTracker(FMemoryTrackerId InTrackerId, const FString InTrackerName);
	~FMemoryTracker();

	FMemoryTrackerId GetId() const { return Id; }
	const FString& GetName() const { return Name; }

	void Update();

private:
	FMemoryTrackerId Id; // the tracker's id
	FString Name; // the tracker's name
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
