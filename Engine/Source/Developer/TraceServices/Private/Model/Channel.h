// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/Channel.h"

namespace Trace {

class FChannelProvider : public IChannelProvider
{
public:
	static const FName ProviderName;

	FChannelProvider();
	void AnnounceChannel(const ANSICHAR* ChannelName, uint32 Id);
	void UpdateChannel(uint32 Id, bool bEnabled);

	virtual uint64	GetChannelCount() const override;
	virtual const TArray<FChannelEntry>& GetChannels() const override;

private:
	TArray<FChannelEntry> Channels;
};

} // namespace Trace