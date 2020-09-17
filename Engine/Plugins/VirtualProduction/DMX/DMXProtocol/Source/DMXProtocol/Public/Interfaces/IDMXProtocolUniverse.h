// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

class IDMXProtocolUniverse
	: public TSharedFromThis<IDMXProtocolUniverse, ESPMode::ThreadSafe>
{
public:
	virtual ~IDMXProtocolUniverse() {}

	virtual IDMXProtocolPtr GetProtocol() const = 0;
	virtual FDMXBufferPtr GetInputDMXBuffer() const = 0;
	virtual FDMXBufferPtr GetOutputDMXBuffer() const = 0;
	virtual void ZeroInputDMXBuffer() = 0;
	virtual void ZeroOutputDMXBuffer() = 0;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) = 0;
	virtual uint8 GetPriority() const = 0;
	virtual uint32 GetUniverseID() const = 0;
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;
	virtual void UpdateSettings(const FJsonObject& InSettings) = 0;
	virtual bool IsSupportRDM() const = 0;

	/** Handle and broadcasting incoming DMX packet */
	virtual void HandleReplyPacket(const FArrayReaderPtr& Buffer) = 0;

	/** Tick on the end of each frame */
	virtual void Tick(float DeltaTime) {};
};
