// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraMessageLogViewModel.h"
#include "NiagaraScriptSourceBase.h"
#include "Modules/ModuleManager.h"
#include "MessageLog/Public/MessageLogModule.h"

FNiagaraMessageLogViewModel::FNiagaraMessageLogViewModel(const FName& InMessageLogName, const FGuid& InMessageLogGuidKey, TSharedPtr<class SWidget>& OutMessageLogWidget)
	: MessageLogGuidKey(InMessageLogGuidKey)
{
	OnMessageManagerRequestRefreshHandle = FNiagaraMessageManager::Get()->GetOnRequestRefresh().AddRaw(this, &FNiagaraMessageLogViewModel::UpdateMessageLog);

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

	// Reuse any existing log, or create a new one (that is not held onto by the message log system)
	auto CreateMessageLogListing = [&MessageLogModule](const FName& LogName)->TSharedRef<IMessageLogListing> {
		FMessageLogInitializationOptions LogOptions;
		// Show Pages so that user is never allowed to clear log messages
		LogOptions.bShowPages = false;
		LogOptions.bShowFilters = false;
		LogOptions.bAllowClear = false;
		LogOptions.MaxPageCount = 1;

		if (MessageLogModule.IsRegisteredLogListing(LogName))
		{
			return MessageLogModule.GetLogListing(LogName);
		}
		else
		{
			return  MessageLogModule.CreateLogListing(LogName, LogOptions);
		}
	};

	MessageLogListing = CreateMessageLogListing(InMessageLogName);
	OutMessageLogWidget = MessageLogModule.CreateLogListingWidget(MessageLogListing.ToSharedRef());
}

FNiagaraMessageLogViewModel::~FNiagaraMessageLogViewModel()
{
	FNiagaraMessageManager::Get()->GetOnRequestRefresh().Remove(OnMessageManagerRequestRefreshHandle);
	FNiagaraMessageManager::Get()->RefreshMessagesForAssetKey(MessageLogGuidKey);
}

void FNiagaraMessageLogViewModel::UpdateMessageLog(const FGuid& InMessageJobBatchAssetKey, const TArray<TSharedRef<const INiagaraMessage>> InNewMessages)
{
	if (MessageLogGuidKey == InMessageJobBatchAssetKey)
	{
		MessageLogListing->ClearMessages();
		TArray<TSharedRef<FTokenizedMessage>> NewTokenizedMessages;
		for (const TSharedRef<const INiagaraMessage> NewMessage : InNewMessages)
		{
			NewTokenizedMessages.Add(NewMessage->GenerateTokenizedMessage());
		}
		MessageLogListing->AddMessages(NewTokenizedMessages);
	}
}

void FNiagaraMessageLogViewModel::SetMessageLogGuidKey(const FGuid& InViewedAssetObjectKey)
{
	MessageLogGuidKey = InViewedAssetObjectKey;
}
