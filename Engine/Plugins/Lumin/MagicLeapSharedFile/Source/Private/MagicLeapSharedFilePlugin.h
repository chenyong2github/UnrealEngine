// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapSharedFilePlugin.h"
#include "Containers/Ticker.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapSharedFile, Verbose, All);

class FMagicLeapSharedFilePlugin : public IMagicLeapSharedFilePlugin
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

	virtual IFileHandle* SharedFileRead(const FString& FileName) override;
	virtual IFileHandle* SharedFileWrite(const FString& FileName) override;
	virtual bool SharedFileListAccessibleFiles(TArray<FString>& OutSharedFileList) override;
	virtual bool SharedFilePickAsync(const FMagicLeapFilesPickedResultDelegate& InResultDelegate) override;

	bool Tick(float DeltaTime);
	void GetFileNamesFromSharedFileList(void* SharedFiles);

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;

	FCriticalSection Mutex;
	FMagicLeapFilesPickedResultDelegate ResultDelegate;
	bool bWaitingForDelegateResult;
	TArray<FString> PickedFileList;
};

inline FMagicLeapSharedFilePlugin& GetMagicLeapSharedFilePlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapSharedFilePlugin>("MagicLeapSharedFile");
}
