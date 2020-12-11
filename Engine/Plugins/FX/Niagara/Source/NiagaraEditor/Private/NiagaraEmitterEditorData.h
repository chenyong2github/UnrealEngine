// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorDataBase.h"

#include "NiagaraEmitterEditorData.generated.h"

class UNiagaraStackEditorData;

/** Editor only UI data for emitters. */
UCLASS()
class UNiagaraEmitterEditorData : public UNiagaraEditorDataBase
{
	GENERATED_BODY()

public:
	UNiagaraEmitterEditorData(const FObjectInitializer& ObjectInitializer);

	virtual void PostLoad() override;

	NIAGARAEDITOR_API UNiagaraStackEditorData& GetStackEditorData() const;

	TRange<float> GetPlaybackRange() const;

	void SetPlaybackRange(TRange<float> InPlaybackRange);

private:
	UPROPERTY(Instanced)
	UNiagaraStackEditorData* StackEditorData;

	UPROPERTY()
	float PlaybackRangeMin;

	UPROPERTY()
	float PlaybackRangeMax;

	void StackEditorDataChanged();
};