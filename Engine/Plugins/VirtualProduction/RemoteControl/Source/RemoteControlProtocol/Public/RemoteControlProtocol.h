// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocol.h"
#include "RemoteControlPreset.h"
#include "RemoteControlProtocolBinding.h"

#include "UObject/StructOnScope.h"

/**
 * Base class implementation for remote control protocol
 */
class FRemoteControlProtocol : public IRemoteControlProtocol
{
public:

	//~ Begin IRemoteControlProtocol interface
	virtual FRemoteControlProtocolEntityPtr CreateNewProtocolEntity(FProperty* InProperty, URemoteControlPreset* InOwner, FGuid InPropertyId) const override
	{
		const FStructOnScope BaseScope(GetProtocolScriptStruct());
		FRemoteControlProtocolEntityPtr NewDataPtr = MakeShared<TStructOnScope<FRemoteControlProtocolEntity>>();
		NewDataPtr->InitializeFrom(BaseScope);

		(*NewDataPtr)->Owner = InOwner;
		(*NewDataPtr)->PropertyId = MoveTemp(InPropertyId);

		return NewDataPtr;
	}
	//~ End IRemoteControlProtocol interface
};
