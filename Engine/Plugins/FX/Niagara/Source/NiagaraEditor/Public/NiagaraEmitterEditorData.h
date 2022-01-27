// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorDataBase.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraStackSection.h"
#include "NiagaraEmitterEditorData.generated.h"

class UNiagaraStackEditorData;


/** Editor only UI data for emitters. */
UCLASS(MinimalAPI)
class UNiagaraEmitterEditorData : public UNiagaraEditorDataBase
{
	GENERATED_BODY()

public: 
	struct PrivateMemberNames
	{
		static const FName SummarySections;
	};

public:
	UNiagaraEmitterEditorData(const FObjectInitializer& ObjectInitializer);

	virtual void PostLoad() override;

	NIAGARAEDITOR_API UNiagaraStackEditorData& GetStackEditorData() const;

	TRange<float> GetPlaybackRange() const;

	void SetPlaybackRange(TRange<float> InPlaybackRange);

	NIAGARAEDITOR_API FSimpleMulticastDelegate& OnSummaryViewStateChanged();
	
	NIAGARAEDITOR_API bool ShouldShowSummaryView() const { return bShowSummaryView; }
	NIAGARAEDITOR_API void ToggleShowSummaryView();
	
	NIAGARAEDITOR_API TMap<FFunctionInputSummaryViewKey, FFunctionInputSummaryViewMetadata> GetSummaryViewMetaDataMap() const;
	NIAGARAEDITOR_API FFunctionInputSummaryViewMetadata GetSummaryViewMetaData(const FFunctionInputSummaryViewKey& Key) const;
	NIAGARAEDITOR_API void SetSummaryViewMetaData(const FFunctionInputSummaryViewKey& Key, const FFunctionInputSummaryViewMetadata& NewMetadata);

	const TArray<FNiagaraStackSection> GetSummarySections() const { return SummarySections; }

private:
	UPROPERTY(Instanced)
	TObjectPtr<UNiagaraStackEditorData> StackEditorData;

	UPROPERTY()
	float PlaybackRangeMin;

	UPROPERTY()
	float PlaybackRangeMax;

	UPROPERTY()
	uint32 bShowSummaryView : 1;

	/** Stores metadata for filtering function inputs when in Filtered/Simple view. */
	UPROPERTY()
	TMap<FFunctionInputSummaryViewKey, FFunctionInputSummaryViewMetadata> SummaryViewFunctionInputMetadata;

	UPROPERTY(EditAnywhere, Category=Summary, meta=(NiagaraNoMerge))
	TArray<FNiagaraStackSection> SummarySections;

	FSimpleMulticastDelegate OnSummaryViewStateChangedDelegate;
	
	void StackEditorDataChanged();	
};


