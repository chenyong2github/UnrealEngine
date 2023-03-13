// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NiagaraSimCacheCapture.h"
#include "ViewModels/NiagaraSimCacheViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"

class FNiagaraSimCacheCapture;
class UNiagaraSimCache;

class SNiagaraDebugCaptureView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraDebugCaptureView)
	{}

	SLATE_END_ARGS();

	DECLARE_DELEGATE(FOnRequestSpreadsheetTab)

	void OnNumFramesChanged(int32 InNumFrames);
	void Construct(const FArguments& InArgs, const TSharedRef<FNiagaraSystemViewModel> SystemViewModel, const TSharedRef<FNiagaraSimCacheViewModel> SimCacheViewModel);
	virtual ~SNiagaraDebugCaptureView() override;
	FOnRequestSpreadsheetTab& OnRequestSpreadsheetTab() {return RequestSpreadsheetTab; }

protected:
	FReply OnSingleFrameSelected();
	FReply OnMultiFrameSelected();
	void OnCaptureComplete(UNiagaraSimCache* CapturedSimCache);
	
	FNiagaraSimCacheCapture SimCacheCapture;
	UNiagaraSimCache* CapturedCache = nullptr;

	UNiagaraComponent* TargetComponent = nullptr;
	int32 NumFrames = 1;
	bool bIsCaptureActive = false;
	TSharedPtr<FNiagaraSimCacheViewModel> SimCacheViewModel;
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	FOnRequestSpreadsheetTab RequestSpreadsheetTab;
	
};
