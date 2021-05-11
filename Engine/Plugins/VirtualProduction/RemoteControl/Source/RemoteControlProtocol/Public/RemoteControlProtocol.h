// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocol.h"

/**
 * Base class implementation for remote control protocol
 */
class REMOTECONTROLPROTOCOL_API FRemoteControlProtocol : public IRemoteControlProtocol
{
public:
	FRemoteControlProtocol(FName InProtocolName);
	~FRemoteControlProtocol();
	
	//~ Begin IRemoteControlProtocol interface
	virtual FRemoteControlProtocolEntityPtr CreateNewProtocolEntity(FProperty* InProperty, URemoteControlPreset* InOwner, FGuid InPropertyId) const override;
	virtual void QueueValue(const FRemoteControlProtocolEntityPtr InProtocolEntity, const double InProtocolValue) override;
	virtual void OnEndFrame() override;
	//~ End IRemoteControlProtocol interface

	/**
	 * Helper function for comparing the Protocol Entity with given Property Id inside returned lambda
	 * @param InPropertyId Property unique Id
	 * @return Lambda function with ProtocolEntityWeakPtr as argument
	 */
	static TFunction<bool(FRemoteControlProtocolEntityWeakPtr InProtocolEntityWeakPtr)> CreateProtocolComparator(FGuid InPropertyId);

protected:
	/** Current Protocol Name */
	FName ProtocolName;

private:
	/** Map of the entities and protocol values about to apply */
	TMap<const FRemoteControlProtocolEntityPtr, double> EntityValuesToApply;

	/**
	 * Map of the entities and protocol values from previous tick.
	 * It allows skipping values that are very close to those of the previous frame
	 */
	TMap<const FRemoteControlProtocolEntityPtr, double> PreviousTickValuesToApply;
};
