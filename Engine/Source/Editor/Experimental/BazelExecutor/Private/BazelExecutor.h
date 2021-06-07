// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IRemoteExecutor.h"
#include "IContentAddressableStorage.h"
#include "IExecution.h"
#include "HAL/RunnableThread.h"

class FBazelCompletionQueueRunnable;


class FBazelExecutor : public IRemoteExecutor
{
public:
	struct FSettings
	{
		FString ContentAddressableStorageTarget;
		FString ExecutionTarget;
		TMap<FString, FString> ContentAddressableStorageHeaders;
		TMap<FString, FString> ExecutionHeaders;
		int32 MaxSendMessageSize;
		int32 MaxReceiveMessageSize;
		FString ContentAddressableStoragePemCertificateChain;
		FString ContentAddressableStoragePemPrivateKey;
		FString ContentAddressableStoragePemRootCertificates;
		FString ExecutionPemCertificateChain;
		FString ExecutionPemPrivateKey;
		FString ExecutionPemRootCertificates;
	};

private:
	TUniquePtr<IContentAddressableStorage> ContentAddressableStorage;
	TUniquePtr<IExecution> Execution;
	TUniquePtr<FRunnableThread> Thread;
	TSharedPtr<FBazelCompletionQueueRunnable> Runnable;

public:
	void Initialize(const FSettings& Settings);
	void Shutdown();

	virtual FName GetFName() const override;
	virtual FText GetNameText() const override;
	virtual FText GetDescriptionText() const override;

	virtual bool CanRemoteExecute() const override;

	IContentAddressableStorage* GetContentAddressableStorage() const override { return ContentAddressableStorage.Get(); }
	IExecution* GetExecution() const override { return Execution.Get(); }
};