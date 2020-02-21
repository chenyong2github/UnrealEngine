// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "DatasmithRemoteEndpointComponent.generated.h"

/**
 * Automatically register/unregister an RemoteImportAnchor while in play
 * Accept a RemoteImport command to load a file by path
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent, Experimental))
class DATASMITHREMOTEIMPORT_API UDatasmithRemoteEndpointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Name that will be exposed to the RemoteImport system */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Anchor)
	FString Name;

private:
	FString RegisteredAnchorName; // Snapshot registered name for correct unregister
	void OnImportFile(const FString& FilePath);
};

