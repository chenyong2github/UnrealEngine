// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "Templates/SharedPointer.h"

class FDMXSignal;

/** */
class DMXPROTOCOL_API IDMXSender
	: public TSharedFromThis<IDMXSender>
{
public:
	virtual ~IDMXSender()
	{}

	/** Sends the DMX signal */
	virtual void SendDMXSignal(const FDMXSignalSharedRef& DMXSignal) = 0;

	/** Clears teh buf*/
	virtual void ClearBuffer() = 0;
};
