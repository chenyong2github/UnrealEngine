// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "TrackerJobs.h"

namespace TraceServices {

class FMetadataDb;

////////////////////////////////////////////////////////////////////////////////
class IRetireeSink
{
public:
	virtual void	Begin(const FMetadataDb* MetadataDb) = 0;
	virtual void	End() = 0;
	virtual void	AddRetirees(uint32 SerialBias, TArrayView<FRetiree> Retirees) = 0;
};

} // namespace TraceServices
