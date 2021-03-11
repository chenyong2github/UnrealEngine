// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocol.h"

/**
 * Base class implementation for remote control protocol
 */
class REMOTECONTROLPROTOCOL_API FRemoteControlProtocol : public IRemoteControlProtocol
{
public:
	//~ Begin IRemoteControlProtocol interface
	virtual FRemoteControlProtocolEntityPtr CreateNewProtocolEntity(FProperty* InProperty, URemoteControlPreset* InOwner, FGuid InPropertyId) const override;
	//~ End IRemoteControlProtocol interface

	/**
	 * Helper function for comparing the Protocol Entity with given Property Id inside returned lambda
	 * @param InPropertyId Property unique Id
	 * @return Lambda function with ProtocolEntityWeakPtr as argument
	 */
	static TFunction<bool(FRemoteControlProtocolEntityWeakPtr InProtocolEntityWeakPtr)> CreateProtocolComparator(FGuid InPropertyId);
};
