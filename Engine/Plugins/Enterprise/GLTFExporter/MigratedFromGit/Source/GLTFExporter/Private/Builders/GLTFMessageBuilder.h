// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBuilder.h"

class IMessageLogListing;

class FGLTFMessageBuilder : public FGLTFBuilder
{
protected:

	FGLTFMessageBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	void AddSuggestionMessage(const FString& Message);

	void AddWarningMessage(const FString& Message);

	void AddErrorMessage(const FString& Message);

	const TArray<FString>& GetSuggestionMessages() const;

	const TArray<FString>& GetWarningMessages() const;

	const TArray<FString>& GetErrorMessages() const;

	void OpenLog() const;

	void ClearLog();

private:

	enum class ELogLevel
	{
		Suggestion,
		Warning,
		Error,
	};

	void PrintToLog(ELogLevel Level, const FString& Message) const;

	TArray<FString> Suggestions;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	TSharedPtr<IMessageLogListing> LogListing;
};
