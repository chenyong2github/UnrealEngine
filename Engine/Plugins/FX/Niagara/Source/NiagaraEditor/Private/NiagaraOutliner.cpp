// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraOutliner.h"
#include "NiagaraDebugger.h"

UNiagaraOutliner::UNiagaraOutliner(const FObjectInitializer& Initializer)
{

}

void UNiagaraOutliner::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Data) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, ViewSettings))
	{
		OnDataChangedDelegate.Broadcast();
	}
	else
	{
		//Delegate will trigger the capture if bTriggerCapture is set.
		OnSettingsChangedDelegate.Broadcast();

		CaptureSettings.bTriggerCapture = false;

		SaveConfig();
	}
}

void UNiagaraOutliner::UpdateData(const FNiagaraOutlinerData& NewData)
{
	FNiagaraOutlinerData::StaticStruct()->CopyScriptStruct(&Data, &NewData);
	Modify();
	OnDataChangedDelegate.Broadcast();
	//TODO: Do some kind of diff on the data and collect any recently removed components etc into their own area.
	//Possibly keep them in the UI optionally but colour/mark them as dead until the user opts to remove them or on some timed interval.
}

const FNiagaraOutlinerWorldData* UNiagaraOutliner::FindWorldData(const FString& WorldName)
{
	return Data.WorldData.Find(WorldName);
}

const FNiagaraOutlinerSystemData* UNiagaraOutliner::FindSystemData(const FString& WorldName, const FString& SystemName)
{
	if (const FNiagaraOutlinerWorldData* WorldData = FindWorldData(WorldName))
	{
		return WorldData->Systems.Find(SystemName);
	}
	return nullptr;
}

const FNiagaraOutlinerSystemInstanceData* UNiagaraOutliner::FindComponentData(const FString& WorldName, const FString& SystemName, const FString& ComponentName)
{
	if (const FNiagaraOutlinerSystemData* SystemData = FindSystemData(WorldName, SystemName))
	{
		return SystemData->SystemInstances.FindByPredicate([&](const FNiagaraOutlinerSystemInstanceData& InData){ return ComponentName == InData.ComponentName;});
	}
	return nullptr;
}

const FNiagaraOutlinerEmitterInstanceData* UNiagaraOutliner::FindEmitterData(const FString& WorldName, const FString& SystemName, const FString& ComponentName, const FString& EmitterName)
{
	if (const FNiagaraOutlinerSystemInstanceData* InstData = FindComponentData(WorldName, SystemName, ComponentName))
	{
		return InstData->Emitters.FindByPredicate([&](const FNiagaraOutlinerEmitterInstanceData& Emitter){ return EmitterName == Emitter.EmitterName;});
	}
	return nullptr;
}
