// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeCompilerLog.h"

#include "StateTreeState.h"
#include "Developer/MessageLog/Public/IMessageLogListing.h"
#include "Misc/UObjectToken.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

void FStateTreeCompilerLog::AppendToLog(IMessageLogListing* LogListing) const
{
	for (const FStateTreeCompilerLogMessage& StateTreeMessage : Messages)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create((EMessageSeverity::Type)StateTreeMessage.Severity);

		if (StateTreeMessage.State != nullptr)
		{
			Message->AddToken(FUObjectToken::Create(StateTreeMessage.State, FText::FromName(StateTreeMessage.State->Name)));
		}

		if (StateTreeMessage.Item.ID.IsValid())
		{
			Message->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LogMessageItem", " {0}"), FText::FromName(StateTreeMessage.Item.Name))));
		}

		if (!StateTreeMessage.Message.IsEmpty())
		{
			Message->AddToken(FTextToken::Create(FText::FromString(StateTreeMessage.Message)));
		}

		LogListing->AddMessage(Message);
	}
}


#undef LOCTEXT_NAMESPACE
