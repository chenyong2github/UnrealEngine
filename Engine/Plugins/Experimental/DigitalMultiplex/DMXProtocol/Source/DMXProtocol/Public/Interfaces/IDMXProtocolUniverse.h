// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

class IDMXProtocolUniverse
{
public:
	virtual ~IDMXProtocolUniverse() {}

	virtual TSharedPtr<IDMXProtocol> GetProtocol() const = 0;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const = 0;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const = 0;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) = 0;
	virtual uint8 GetPriority() const = 0;
	virtual uint32 GetUniverseID() const = 0;
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;
	virtual bool IsSupportRDM() const = 0;
};
