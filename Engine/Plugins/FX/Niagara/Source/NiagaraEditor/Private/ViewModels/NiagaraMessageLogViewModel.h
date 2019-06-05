// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Templates/SharedPointer.h"
#include "Developer/MessageLog/Public/IMessageLogListing.h"
#include "NiagaraMessageManager.h"

class FNiagaraMessageLogViewModel : public TSharedFromThis<FNiagaraMessageLogViewModel>
{
public:
	FNiagaraMessageLogViewModel(const FName& InMessageLogName, const FGuid& InMessageLogGuidKey, TSharedPtr<class SWidget>& OutMessageLogWidget );

	~FNiagaraMessageLogViewModel();

	void UpdateMessageLog(const FGuid& InMessageJobBatchAssetKey, const TArray<TSharedRef<const INiagaraMessage>> InNewMessages);

	void SetMessageLogGuidKey(const FGuid& InMessageLogGuidKey);

private:
	TSharedPtr<IMessageLogListing> MessageLogListing;
	FGuid MessageLogGuidKey;

	FDelegateHandle OnMessageManagerRequestRefreshHandle;
};