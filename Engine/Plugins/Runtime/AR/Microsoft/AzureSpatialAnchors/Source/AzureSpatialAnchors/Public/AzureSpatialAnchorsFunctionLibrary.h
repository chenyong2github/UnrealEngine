// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ARPin.h"
#include "AzureSpatialAnchorsTypes.h"
#include "AzureCloudSpatialAnchor.h"

#include "AzureSpatialAnchorsFunctionLibrary.generated.h"


/** A function library that provides static/Blueprint functions for AzureSpatialAnchors.*/
UCLASS()
class AZURESPATIALANCHORS_API UAzureSpatialAnchorsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	///**
	// * Create an ASA session.  
	// * It is not yet active.
	// *
	// * @return (Boolean)  True if a session has been created (even if it already existed).
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static bool CreateSession();

	///**
	// * Configure the ASA session.  
	// * This will take effect when the next session is started.
	// * 
	// * @param AccountId      The ARPin to save.
	// * @param AccountKey		The ARPin hosting result.
	// * @param LogVerbosity	A new instance of UCloudARPin created using the input ARPinToHost.
	// *
	// * @return (Boolean)  True if the session configuration was set.
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static bool ConfigSession(const FString& AccountId, const FString& AccountKey, EAzureSpatialAnchorsLogVerbosity LogVerbosity);

	///**
	// * Start a Session running.  
	// * ASA will start collecting tracking data.
	// *
	// * @return (Boolean)  True if a session has been started (even if it was already started).
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static bool StartSession();

	///**
	// * The session will stop, it can be started again.
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static bool StopSession();

	///**
	// * The session will be destroyed.
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static bool DestroySession();

	///**
	// * Get the cloud anchor associated with a particular ARPin.
	// *
	// * @param ARPin      The ARPin who's cloud anchor we hope to get.
	// * @param OutAzureCloudSpatialAnchor	The cloud spatial anchor, or null.
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static void GetCloudAnchor(UARPin* ARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor);

	///**
	// * Get list of all CloudAnchors.
	// *
	// * @param OutCloudAnchors 	The cloud spatial anchors
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Keywords = "azure spatial anchor hololens wmr pin ar all"))	
	static void GetCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors);

	///**
	// * Save the pin to the cloud.
	// * This will start a Latent Action to save the ARPin to the Azure Spatial Anchors cloud service.
	// *
	// * @param ARPin      The ARPin to save.
	// * @param InMinutesFromNow      The expiration time of the cloud pin.  <= 0 means no expiration.
	// * @param OutAzureCloudSpatialAnchor  The Cloud anchor handle.
	// * @param OutResult	Result of the Save attempt.
	// * @param OutErrorString	Additional information about the OUtResult (may be empty).
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static void SavePinToCloud(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UARPin* ARPin, int InMinutesFromNow, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString);

	///**
	// * Delete the cloud anchor in the cloud.
	// * This will start a Latent Action to delete the cloud anchor from the cloud service.
	// *
	// * @param CloudSpatialAnchor      The Cloud anchor to delete.
	// * @param OutResult	Result of the Delete attempt.
	// * @param OutErrorString	Additional information about the OutResult (may be empty).
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static void DeleteCloudAnchor(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UAzureCloudSpatialAnchor* CloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString);

	///**
	// * Load a pin from the cloud..
	// * This will start a Latent Action to load a cloud anchor and create a pin for it.
	// *
	// * @param CloudId						The Id of the cloud anchor we will try to load.
	// * @param PinId						Specify the name of the Pin to load into.  Will fail if a pin of this name already exists.  If empty an auto-generated id will be used.
	// * @param OutARPin					Filled in with the pin created, if successful.
	// * @param OutAzureCloudSpatialAnchor	Filled in with the cloud spatial anchor created, if successful.
	// * @param OutResult					The Result enumeration.
	// * @param OutErrorString				Additional informatiuon about the OutResult (may be empty).
	// */
	UFUNCTION(BlueprintCallable, Category = "AzureSpatialAnchors", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", Keywords = "azure spatial anchor hololens wmr pin ar all"))
	static void LoadCloudAnchor(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, FString CloudId, FString PinId, UARPin*& OutARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString);
};