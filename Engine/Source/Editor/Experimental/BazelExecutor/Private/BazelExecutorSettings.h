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
	/** The Bazel server address. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "Target", ConfigRestartRequired = true))
	FString Target;
	
	/** The Bazel PEM certificate chain". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "PEM Certificate Chain", ConfigRestartRequired = true))
	FString PemCertificateChain;

	/** The Bazel PEM private key". */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "PEM Private Key", ConfigRestartRequired = true))
	FString PemPrivateKey;

	/** The Bazel pem root certificates. */
	UPROPERTY(Config, EditAnywhere, Category = "Bazel", meta = (DisplayName = "PEM Root Certificates", ConfigRestartRequired = true))
	FString PemRootCertificates;

};
