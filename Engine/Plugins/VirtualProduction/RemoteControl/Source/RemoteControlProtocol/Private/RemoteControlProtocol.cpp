// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocol.h"

#include "RemoteControlPreset.h"
#include "RemoteControlProtocolBinding.h"
#include "RemoteControlProtocolModule.h"

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

FProperty* IRemoteControlProtocol::GetRangeInputTemplateProperty() const
{
	FProperty* RangeInputTemplateProperty = nullptr;
	if(UScriptStruct* ProtocolScriptStruct = GetProtocolScriptStruct())
	{
		RangeInputTemplateProperty = ProtocolScriptStruct->FindPropertyByName("RangeInputTemplate");
	}

	if(!ensure(RangeInputTemplateProperty))
	{
		UE_LOG(LogRemoteControlProtocol, Warning, TEXT("Could not find RangeInputTemplate Property for this Protocol. Please either add this named property to the ProtocolScriptStruct implementation, or override IRemoteControlProtocol::GetRangeTemplateType."));
	}
	
	return RangeInputTemplateProperty;
}
