// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFMessageBuilder.h"
#include "GLTFExporterModule.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Misc/FeedbackContext.h"

FGLTFMessageBuilder::FGLTFMessageBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FGLTFBuilder(FilePath, ExportOptions)
{
	if (!FApp::IsUnattended())
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		LogListing = MessageLogModule.GetLogListing(GLTFEXPORTER_MODULE_NAME);
		LogListing->SetLabel(FText::FromString(GLTFEXPORTER_FRIENDLY_NAME));
	}
}

void FGLTFMessageBuilder::AddInfoMessage(const FString& Message)
{
	Infos.Add(Message);
	PrintToLog(ELogLevel::Info, Message);
}

void FGLTFMessageBuilder::AddWarningMessage(const FString& Message)
{
	Warnings.Add(Message);
	PrintToLog(ELogLevel::Warning, Message);
}

void FGLTFMessageBuilder::AddErrorMessage(const FString& Message)
{
	Errors.Add(Message);
	PrintToLog(ELogLevel::Error, Message);
}

const TArray<FString>& FGLTFMessageBuilder::GetInfoMessages() const
{
	return Infos;
}

const TArray<FString>& FGLTFMessageBuilder::GetWarningMessages() const
{
	return Warnings;
}

const TArray<FString>& FGLTFMessageBuilder::GetErrorMessages() const
{
	return Errors;
}

void FGLTFMessageBuilder::OpenLog() const
{
	if (LogListing != nullptr)
	{
		LogListing->Open();
	}
}

void FGLTFMessageBuilder::ClearLog()
{
	Infos.Empty();
	Warnings.Empty();
	Errors.Empty();

	if (LogListing != nullptr)
	{
		LogListing->ClearMessages();
	}
}

void FGLTFMessageBuilder::PrintToLog(ELogLevel Level, const FString& Message) const
{
#if !NO_LOGGING
	ELogVerbosity::Type Verbosity;

	switch (Level)
	{
		case ELogLevel::Info:    Verbosity = ELogVerbosity::Display; break;
		case ELogLevel::Warning: Verbosity = ELogVerbosity::Warning; break;
		case ELogLevel::Error:   Verbosity = ELogVerbosity::Error; break;
		default:
			checkNoEntry();
			return;
	}

	GWarn->Log(LogGLTFExporter.GetCategoryName(), Verbosity, Message);
#endif

	if (LogListing != nullptr)
	{
		EMessageSeverity::Type Severity;

		switch (Level)
		{
			case ELogLevel::Info:    Severity = EMessageSeverity::Info; break;
			case ELogLevel::Warning: Severity = EMessageSeverity::Warning; break;
			case ELogLevel::Error:   Severity = EMessageSeverity::Error; break;
			default:
				checkNoEntry();
				return;
		}

		LogListing->AddMessage(FTokenizedMessage::Create(Severity, FText::FromString(Message)), false);
	}
}
