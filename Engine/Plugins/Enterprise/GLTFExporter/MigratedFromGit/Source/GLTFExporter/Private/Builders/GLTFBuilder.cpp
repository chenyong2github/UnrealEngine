// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"

DEFINE_LOG_CATEGORY(LogGLTFExporter);

namespace
{
	// TODO: turn into config variable?
	const TCHAR* LogListingName = TEXT("GLTFExporter");
	const TCHAR* LogListingLabel = TEXT("GLTF Exporter");
} // anonymous namespace

FGLTFBuilder::FGLTFBuilder()
{
}

FGLTFBuilder::~FGLTFBuilder()
{
}

void FGLTFBuilder::ClearLogMessages()
{
	LogMessages.Empty();
}

void FGLTFBuilder::AddLogMessage(EGLTFMessageSeverity Severity, const FString& Message)
{
	LogMessages.Emplace(Severity, Message);
}

void FGLTFBuilder::AddInfoMessage(const FString& Message)
{
	AddLogMessage(EGLTFMessageSeverity::Info, Message);
}

void FGLTFBuilder::AddWarningMessage(const FString& Message)
{
	AddLogMessage(EGLTFMessageSeverity::Warning, Message);
}

void FGLTFBuilder::AddErrorMessage(const FString& Message)
{
	AddLogMessage(EGLTFMessageSeverity::Error, Message);
}

const TArray<FGLTFBuilder::FLogMessage>& FGLTFBuilder::GetLogMessages() const
{
	return LogMessages;
}

TArray<FGLTFBuilder::FLogMessage> FGLTFBuilder::GetLogMessages(EGLTFMessageSeverity Severity) const
{
	return LogMessages.FilterByPredicate(
		[Severity](const FLogMessage& LogMessage)
		{
			return LogMessage.Key == Severity;
		});
}

TArray<FGLTFBuilder::FLogMessage> FGLTFBuilder::GetInfoMessages() const
{
	return GetLogMessages(EGLTFMessageSeverity::Info);
}

TArray<FGLTFBuilder::FLogMessage> FGLTFBuilder::GetWarningMessages() const
{
	return GetLogMessages(EGLTFMessageSeverity::Warning);
}

TArray<FGLTFBuilder::FLogMessage> FGLTFBuilder::GetErrorMessages() const
{
	return GetLogMessages(EGLTFMessageSeverity::Error);
}

int32 FGLTFBuilder::GetLogMessageCount() const
{
	return LogMessages.Num();
}

int32 FGLTFBuilder::GetInfoMessageCount() const
{
	return GetInfoMessages().Num();
}

int32 FGLTFBuilder::GetWarningMessageCount() const
{
	return GetWarningMessages().Num();
}

int32 FGLTFBuilder::GetErrorMessageCount() const
{
	return GetErrorMessages().Num();
}

void FGLTFBuilder::ShowLogMessages() const
{
	if (LogMessages.Num() > 0)
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		const TSharedRef<IMessageLogListing> LogListing = MessageLogModule.GetLogListing(LogListingName);

		LogListing->SetLabel(FText::FromString(LogListingLabel));
		LogListing->ClearMessages();

		for (const FLogMessage& Message : LogMessages)
		{
			LogListing->AddMessage(CreateTokenizedMessage(Message));
		}

		MessageLogModule.OpenMessageLog(LogListingName);
	}
}

void FGLTFBuilder::WriteLogMessagesToConsole() const
{
	for (const FLogMessage& Message : LogMessages)
	{
		WriteLogMessageToConsole(Message);
	}
}

void FGLTFBuilder::WriteLogMessageToConsole(const FLogMessage& LogMessage)
{
	switch (LogMessage.Key)
	{
		case EGLTFMessageSeverity::Info: UE_LOG(LogGLTFExporter, Display, TEXT("%s"), *LogMessage.Value); break;
		case EGLTFMessageSeverity::Warning: UE_LOG(LogGLTFExporter, Warning, TEXT("%s"), *LogMessage.Value); break;
		case EGLTFMessageSeverity::Error: UE_LOG(LogGLTFExporter, Error, TEXT("%s"), *LogMessage.Value); break;
		default:
			checkNoEntry();
			break;
	}
}

TSharedRef<FTokenizedMessage> FGLTFBuilder::CreateTokenizedMessage(const FLogMessage& LogMessage)
{
	EMessageSeverity::Type MessageSeverity = EMessageSeverity::Type::CriticalError;

	switch (LogMessage.Key)
	{
		case EGLTFMessageSeverity::Info: MessageSeverity = EMessageSeverity::Type::Info; break;
		case EGLTFMessageSeverity::Warning: MessageSeverity = EMessageSeverity::Type::Warning; break;
		case EGLTFMessageSeverity::Error: MessageSeverity = EMessageSeverity::Type::Error; break;
		default:
			checkNoEntry();
			break;
	}

	return FTokenizedMessage::Create(MessageSeverity, FText::FromString(LogMessage.Value));
}
