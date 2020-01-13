// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Containers/UnrealString.h"

namespace Trace {

struct FChannelEntry
{
	uint32 Id;
	FString Name;
	bool bIsEnabled;
};

class IChannelProvider
	: public IProvider
{
public:
	virtual ~IChannelProvider() = default;
	virtual uint64	GetChannelCount() const = 0;
	virtual const TArray<FChannelEntry>& GetChannels() const = 0;
};

} // namespace Trace