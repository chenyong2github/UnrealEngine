// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocol.h"

#include "RemoteControlPreset.h"
#include "RemoteControlProtocolBinding.h"

#include "UObject/StructOnScope.h"

FRemoteControlProtocolEntityPtr FRemoteControlProtocol::CreateNewProtocolEntity(FProperty* InProperty, URemoteControlPreset* InOwner, FGuid InPropertyId) const
{
	FRemoteControlProtocolEntityPtr NewDataPtr = MakeShared<TStructOnScope<FRemoteControlProtocolEntity>>();
	NewDataPtr->InitializeFrom(FStructOnScope(GetProtocolScriptStruct()));
	(*NewDataPtr)->Init(InOwner, InPropertyId);

	return NewDataPtr;
}

TFunction<bool(FRemoteControlProtocolEntityWeakPtr InProtocolEntityWeakPtr)> FRemoteControlProtocol::CreateProtocolComparator(FGuid InPropertyId)
{
	return [InPropertyId](FRemoteControlProtocolEntityWeakPtr InProtocolEntityWeakPtr) -> bool
	{
		if (FRemoteControlProtocolEntityPtr ProtocolEntityPtr = InProtocolEntityWeakPtr.Pin())
		{
			return InPropertyId == (*ProtocolEntityPtr)->GetPropertyId();
		}

		return false;
	};
}
