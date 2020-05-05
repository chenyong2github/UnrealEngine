// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

class IDMXProtocolUniverse
{
public:
	virtual ~IDMXProtocolUniverse() {}

	virtual IDMXProtocolPtr GetProtocol() const = 0;
	virtual FDMXBufferPtr GetInputDMXBuffer() const = 0;
	virtual FDMXBufferPtr GetOutputDMXBuffer() const = 0;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) = 0;
	virtual uint8 GetPriority() const = 0;
	virtual uint32 GetUniverseID() const = 0;
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;
	virtual bool IsSupportRDM() const = 0;

	/** Tick on the end of each frame */
	virtual void Tick(float DeltaTime) {};
};
