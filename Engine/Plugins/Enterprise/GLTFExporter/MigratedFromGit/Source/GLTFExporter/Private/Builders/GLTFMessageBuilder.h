// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBuilder.h"

class IMessageLogListing;

class FGLTFMessageBuilder : public FGLTFBuilder
{
protected:

	FGLTFMessageBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	void AddInfoMessage(const FString& Message);

	void AddWarningMessage(const FString& Message);

	void AddErrorMessage(const FString& Message);

	const TArray<FString>& GetInfoMessages() const;

	const TArray<FString>& GetWarningMessages() const;

	const TArray<FString>& GetErrorMessages() const;

	void OpenLog() const;

	void ClearLog();

private:

	enum class ELogLevel
	{
		Info,
		Warning,
		Error,
	};

	void PrintToLog(ELogLevel Level, const FString& Message) const;

	TArray<FString> Infos;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	TSharedPtr<IMessageLogListing> LogListing;
};
