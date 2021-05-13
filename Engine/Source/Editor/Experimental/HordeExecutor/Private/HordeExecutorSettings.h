// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HordeExecutorSettings.generated.h"

UCLASS(config = EditorSettings)
class UHordeExecutorSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** The horde server address. */
	UPROPERTY(Config, EditAnywhere, Category = "Horde", meta = (DisplayName = "Target", ConfigRestartRequired = true))
	FString Target;
	
	/** The horde PEM certificate chain". */
	UPROPERTY(Config, EditAnywhere, Category = "Horde", meta = (DisplayName = "PEM Certificate Chain", ConfigRestartRequired = true))
	FString PemCertificateChain;

	/** The horde PEM private key". */
	UPROPERTY(Config, EditAnywhere, Category = "Horde", meta = (DisplayName = "PEM Private Key", ConfigRestartRequired = true))
	FString PemPrivateKey;

	/** The horde pem root certificates. */
	UPROPERTY(Config, EditAnywhere, Category = "Horde", meta = (DisplayName = "PEM Root Certificates", ConfigRestartRequired = true))
	FString PemRootCertificates;

};
