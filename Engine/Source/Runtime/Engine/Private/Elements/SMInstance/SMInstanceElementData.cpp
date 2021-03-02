// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Components/InstancedStaticMeshComponent.h"
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

bool IsValidComponentForSMInstanceElements(const UInstancedStaticMeshComponent* InComponent)
{
	if (!InComponent)
	{
		return false;
	}

	if (const AActor* OwnerActor = InComponent->GetOwner())
	{
		// Foliage actors have extra bookkeeping data which isn't correctly updated by static mesh instance elements
		// Disable being able to create static mesh instance elements for foliage actors until this is resolved...
		// Note: This test is by name as we cannot link directly to AInstancedFoliageActor
		static const FName NAME_InstancedFoliageActor = "InstancedFoliageActor";
		for (const UClass* ActorClass = OwnerActor->GetClass(); ActorClass; ActorClass = ActorClass->GetSuperClass())
		{
			if (ActorClass->GetFName() == NAME_InstancedFoliageActor)
			{
				return false;
			}
		}
	}

	return true;
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
