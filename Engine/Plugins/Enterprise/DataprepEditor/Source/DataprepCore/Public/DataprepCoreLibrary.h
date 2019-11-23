// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "DataprepCoreLibrary.generated.h"

class UDataprepAssetInterface;
class IDataprepLogger;
class IDataprepProgressReporter;

UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Dataprep Core Blueprint Library"))
class DATAPREPCORE_API UDataprepCoreLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Runs the Dataprep asset's producers, execute its recipe finally runs the consumer.
	 * @param	DataprepAssetInterface		Dataprep asset to run.
	 * @param	ActorsCreated				Array of actors added to the Editor's world.
	 * @param	AssetsCreated				Array of assets created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint | Dataprep")
	static void Execute( UDataprepAssetInterface* DataprepAssetInterface, TArray<AActor*>& ActorsCreated, TArray<UObject*>& AssetsCreated );

	/**
	 * Same as Execute except that progress is reported in the UI and no outputs
	 * @param	DataprepAssetInterface		Dataprep asset to run.
	 * @return	True if successful.
	 */
	static bool ExecuteWithReporting(UDataprepAssetInterface* DataprepAssetInterface);

private:
	static bool Execute_Internal(UDataprepAssetInterface* DataprepAssetInterface, TSharedPtr< IDataprepLogger >& Logger, TSharedPtr< IDataprepProgressReporter >& Reporter );
};