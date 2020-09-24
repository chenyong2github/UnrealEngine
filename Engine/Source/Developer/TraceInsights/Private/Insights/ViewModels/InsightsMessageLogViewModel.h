// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Templates/SharedPointer.h"
#include "Developer/MessageLog/Public/IMessageLogListing.h"

class FInsightsMessageLogViewModel : public TSharedFromThis<FInsightsMessageLogViewModel>
{
public:
	FInsightsMessageLogViewModel(const FName& InMessageLogName, TSharedPtr<class SWidget>& OutMessageLogWidget );
	~FInsightsMessageLogViewModel();

	void UpdateMessageLog(const TArray<TSharedRef<FTokenizedMessage>> InNewMessages);

	void ClearMessageLog();

private:
	TSharedPtr<IMessageLogListing> MessageLogListing;
};
