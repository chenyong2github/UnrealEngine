// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BazelExecutorSettings.generated.h"

UCLASS(config = EditorSettings)
class UBazelExecutorSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** The Bazel server content addressable storage address. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Content Addressable Storage Target", ConfigRestartRequired = true))
	FString ContentAddressableStorageTarget;

	/** The Bazel server execution address. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Execution Target", ConfigRestartRequired = true))
	FString ExecutionTarget;

	/** Extra headers required for content addressable storage requests. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Content Addressable Storage Headers", ConfigRestartRequired = true))
	TMap<FString, FString> ContentAddressableStorageHeaders;

	/** Extra headers required for execution requests. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Execution Headers", ConfigRestartRequired = true))
	TMap<FString, FString> ExecutionHeaders;

	/** Maximum send message size. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Max Send Message Size", ConfigRestartRequired = true))
	int32 MaxSendMessageSize;

	/** Maximum receive message size. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Max Receive Message Size", ConfigRestartRequired = true))
	int32 MaxReceiveMessageSize;
	
	/** The Bazel PEM certificate chain". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Content Addressable Storage PEM Certificate Chain", ConfigRestartRequired = true))
	FString ContentAddressableStoragePemCertificateChain;

	/** The Bazel PEM private key". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Content Addressable Storage PEM Private Key", ConfigRestartRequired = true))
	FString ContentAddressableStoragePemPrivateKey;

	/** The Bazel pem root certificates. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Content Addressable Storage PEM Root Certificates", ConfigRestartRequired = true))
	FString ContentAddressableStoragePemRootCertificates;

	/** The Bazel PEM certificate chain". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Execution PEM Certificate Chain", ConfigRestartRequired = true))
	FString ExecutionPemCertificateChain;

	/** The Bazel PEM private key". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Execution PEM Private Key", ConfigRestartRequired = true))
	FString ExecutionPemPrivateKey;

	/** The Bazel pem root certificates. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Execution PEM Root Certificates", ConfigRestartRequired = true))
	FString ExecutionPemRootCertificates;
};
