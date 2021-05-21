// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterEditorData.h"
#include "NiagaraStackEditorData.h"

UNiagaraEmitterEditorData::UNiagaraEmitterEditorData(const FObjectInitializer& ObjectInitializer)
{
	StackEditorData = ObjectInitializer.CreateDefaultSubobject<UNiagaraStackEditorData>(this, TEXT("StackEditorData"));

	if (StackEditorData != nullptr)
	{
		StackEditorData->OnPersistentDataChanged().AddUObject(this, &UNiagaraEmitterEditorData::StackEditorDataChanged);
	}
	
	PlaybackRangeMin = 0;
	PlaybackRangeMax = 10;
}

void UNiagaraEmitterEditorData::PostLoad()
{
	Super::PostLoad();
	if (StackEditorData == nullptr)
	{
		StackEditorData = NewObject<UNiagaraStackEditorData>(this, TEXT("StackEditorData"), RF_Transactional);
		StackEditorData->OnPersistentDataChanged().AddUObject(this, &UNiagaraEmitterEditorData::StackEditorDataChanged);
	}
	StackEditorData->ConditionalPostLoad();
}

UNiagaraStackEditorData& UNiagaraEmitterEditorData::GetStackEditorData() const
{
	return *StackEditorData;
}

TRange<float> UNiagaraEmitterEditorData::GetPlaybackRange() const
{
	return TRange<float>(PlaybackRangeMin, PlaybackRangeMax);
}

void UNiagaraEmitterEditorData::SetPlaybackRange(TRange<float> InPlaybackRange)
{
	PlaybackRangeMin = InPlaybackRange.GetLowerBoundValue();
	PlaybackRangeMax = InPlaybackRange.GetUpperBoundValue();

	OnPersistentDataChanged().Broadcast();
}

void UNiagaraEmitterEditorData::StackEditorDataChanged()
{
	OnPersistentDataChanged().Broadcast();
}