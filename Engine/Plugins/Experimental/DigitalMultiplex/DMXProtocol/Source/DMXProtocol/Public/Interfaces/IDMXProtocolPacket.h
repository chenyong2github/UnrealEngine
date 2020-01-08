// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

struct IDMXProtocolPacket
{
public:
	virtual ~IDMXProtocolPacket() {}

	virtual TSharedPtr<FBufferArchive> Pack() { return nullptr; };
};
