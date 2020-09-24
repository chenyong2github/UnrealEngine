// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsMessageLogViewModel.h"
#include "Modules/ModuleManager.h"
#include "MessageLog/Public/MessageLogModule.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsMessageLogViewModel::FInsightsMessageLogViewModel(const FName& InMessageLogName, TSharedPtr<class SWidget>& OutMessageLogWidget)
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

	auto CreateMessageLogListing = [&MessageLogModule, &InMessageLogName](const FName& LogName)->TSharedRef<IMessageLogListing> {
		FMessageLogInitializationOptions LogOptions;
		LogOptions.bShowPages = false;
		LogOptions.bShowFilters = false;
		LogOptions.bAllowClear = false;
		LogOptions.MaxPageCount = 1;

		MessageLogModule.RegisterLogListing(InMessageLogName, NSLOCTEXT("InsightsMesssageLog", "InsightsLog", "Insights Log"), LogOptions);
		if (MessageLogModule.IsRegisteredLogListing(LogName))
		{
			return MessageLogModule.GetLogListing(LogName);
		}
		else
		{
			return  MessageLogModule.CreateLogListing(LogName, LogOptions);
		}
	};

	MessageLogModule.EnableMessageLogDisplay(true);

	MessageLogListing = CreateMessageLogListing(InMessageLogName);
	OutMessageLogWidget = MessageLogModule.CreateLogListingWidget(MessageLogListing.ToSharedRef());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsMessageLogViewModel::~FInsightsMessageLogViewModel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsMessageLogViewModel::UpdateMessageLog(const TArray<TSharedRef<FTokenizedMessage>> InNewMessages)
{
	TArray<TSharedRef<FTokenizedMessage>> NewTokenizedMessages;
	MessageLogListing->AddMessages(InNewMessages);
	MessageLogListing->NotifyIfAnyMessages(FText(), EMessageSeverity::Info, false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsMessageLogViewModel::ClearMessageLog()
{
	MessageLogListing->ClearMessages();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
