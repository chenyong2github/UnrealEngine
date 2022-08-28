// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExportOptions.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGLTFExporter, Log, All);

enum class EGLTFMessageSeverity
{
	Info,
	Warning,
	Error
};

class FGLTFBuilder
{
public:

	const UGLTFExportOptions* const ExportOptions;

	FGLTFBuilder(const UGLTFExportOptions* ExportOptions);
	virtual ~FGLTFBuilder();

	typedef TTuple<EGLTFMessageSeverity, FString> FLogMessage;

	void ClearLogMessages();

	void AddLogMessage(EGLTFMessageSeverity Severity, const FString& Message);

	void AddInfoMessage(const FString& Message);

	void AddWarningMessage(const FString& Message);

	void AddErrorMessage(const FString& Message);

	const TArray<FLogMessage>& GetLogMessages() const;

	TArray<FLogMessage> GetLogMessages(EGLTFMessageSeverity Severity) const;

	TArray<FLogMessage> GetInfoMessages() const;

	TArray<FLogMessage> GetWarningMessages() const;

	TArray<FLogMessage> GetErrorMessages() const;

	int32 GetLogMessageCount() const;

	int32 GetInfoMessageCount() const;

	int32 GetWarningMessageCount() const;

	int32 GetErrorMessageCount() const;

	void ShowLogMessages() const;

	void WriteLogMessagesToConsole() const;

protected:

	TArray<FLogMessage> LogMessages;

	static void WriteLogMessageToConsole(const FLogMessage& LogMessage);

	static TSharedRef<FTokenizedMessage> CreateTokenizedMessage(const FLogMessage& LogMessage);
};
