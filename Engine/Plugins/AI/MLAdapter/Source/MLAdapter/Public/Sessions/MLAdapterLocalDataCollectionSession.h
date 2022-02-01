// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Sessions/MLAdapterSession.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MLAdapterLocalDataCollectionSession.generated.h"

/**
 * Collects data from agents' sensors and writes them to a file for offline processing. Only works with a locally
 * controlled, single-player game.
 */
UCLASS(Blueprintable, EditInlineNew)
class MLADAPTER_API UMLAdapterLocalDataCollectionSession : public UMLAdapterSession
{
	GENERATED_BODY()
public:
	virtual void OnPostWorldInit(UWorld& World) override;

	UFUNCTION()
	virtual void OnPawnControllerChanged(APawn* InPawn, AController* InController);

	virtual void Tick(float DeltaTime) override;

	virtual void Close() override;

	UPROPERTY(EditAnywhere, Category = MLAdapter)
	FString FileName;

private:

	UPROPERTY()
	TWeakObjectPtr<UMLAdapterAgent> PlayerControlledAgent;
};
