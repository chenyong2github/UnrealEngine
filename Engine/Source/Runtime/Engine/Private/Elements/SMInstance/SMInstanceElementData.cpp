// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "HAL/IConsoleManager.h"

UE_DEFINE_TYPED_ELEMENT_DATA_RTTI(FSMInstanceElementData);

namespace SMInstanceElementDataUtil
{

static int32 GEnableSMInstanceElements = 0;
static FAutoConsoleVariableRef CVarEnableSMInstanceElements(
	TEXT("TypedElements.EnableSMInstanceElements"),
	GEnableSMInstanceElements,
	TEXT("Is support for static mesh instance elements enabled?")
	);

bool SMInstanceElementsEnabled()
{
	return GEnableSMInstanceElements != 0;
}

FSMInstanceId GetSMInstanceFromHandle(const FTypedElementHandle& InHandle, const bool bSilent)
{
	const FSMInstanceElementData* SMInstanceElement = InHandle.GetData<FSMInstanceElementData>(bSilent);
	return SMInstanceElement ? FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(SMInstanceElement->InstanceElementId) : FSMInstanceId();
}

FSMInstanceId GetSMInstanceFromHandleChecked(const FTypedElementHandle& InHandle)
{
	const FSMInstanceElementData& SMInstanceElement = InHandle.GetDataChecked<FSMInstanceElementData>();
	FSMInstanceId SMInstanceId = FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(SMInstanceElement.InstanceElementId);
	checkf(SMInstanceId, TEXT("Static Mesh Instance Element ID failed to map to a valid Static Mesh Instance Index!"));
	return SMInstanceId;
}

} // namespace SMInstanceElementDataUtil
