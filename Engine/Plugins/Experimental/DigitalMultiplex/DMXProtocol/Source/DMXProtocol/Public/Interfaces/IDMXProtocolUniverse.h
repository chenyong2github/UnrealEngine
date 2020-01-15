// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

class DMXPROTOCOL_API IDMXProtocolUniverse
{
public:
	virtual ~IDMXProtocolUniverse() {}

	virtual IDMXProtocol* GetProtocol() const = 0;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const = 0;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const = 0;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) = 0;
	virtual uint8 GetPriority() const = 0;
	virtual uint16 GetUniverseID() const = 0;
	virtual TWeakPtr<IDMXProtocolPort> GetCachedUniversePort() const = 0;
};
