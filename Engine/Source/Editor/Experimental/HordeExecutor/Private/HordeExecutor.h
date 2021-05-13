// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IRemoteExecutor.h"
#include "IContentAddressableStorage.h"
#include "IExecution.h"
#include "Messages.h"


class FHordeExecutor : public IRemoteExecutor
{
public:
	struct FSslCredentialsOptions
	{
		FString PemCertChain;
		FString PemPrivateKey;
		FString PemRootCerts;
	};

private:
	TUniquePtr<IContentAddressableStorage> ContentAddressableStorage;
	TUniquePtr<IExecution> Execution;

public:
	void Initialize(const FString& Target, const FSslCredentialsOptions& SslCredentialsOptions);

	virtual FName GetFName() const override;
	virtual FText GetNameText() const override;
	virtual FText GetDescriptionText() const override;

	virtual bool CanRemoteExecute() const override;

	IContentAddressableStorage* GetContentAddressableStorage() const override { return ContentAddressableStorage.Get(); }
	IExecution* GetExecution() const override { return Execution.Get(); }
};