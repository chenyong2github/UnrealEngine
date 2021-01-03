// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "TrackerJobs.h"

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class IRetireeSink
{
public:
	struct FRetirements
	{
		TArrayView<FRetiree>		Retirees;
		uint32						SerialBias;
	};

	virtual void RetireAllocs(const FRetirements& Retirement) = 0;
};

} // namespace TraceServices
