// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAzureSpatialAnchors.h"
#include "AzureCloudSpatialAnchor.h"
#include "MixedRealityInterop.h"
#include "UObject/GCObject.h"

class FAzureSpatialAnchorsForWMR : public IAzureSpatialAnchors, public IModuleInterface, public FGCObject
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool CreateSession() override;
	virtual bool ConfigSession(const FString& AccountId, const FString& AccountKey, EAzureSpatialAnchorsLogVerbosity LogVerbosity) override;
	virtual bool StartSession() override;
	virtual void StopSession() override;
	virtual void DestroySession() override;

	virtual bool GetCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) override;
	virtual void GetCloudAnchors(TArray<class UAzureCloudSpatialAnchor*>& OutCloudAnchors) override;
	virtual bool CreateCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) override;
	virtual bool SetCloudAnchorExpiration(class UAzureCloudSpatialAnchor*& InCloudAnchor, int MinutesFromNow) override;
	virtual bool SaveCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool SaveCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void SaveCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) override;
	virtual bool DeleteCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool DeleteCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void DeleteCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) override;
	virtual bool LoadCloudAnchorByIDAsync_Start(class FPendingLatentAction* LatentAction, const FString& InCloudAnchorIdentifier, const FString& InLocalAnchorId, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool LoadCloudAnchorByIDAsync_Update(class FPendingLatentAction* LatentAction, class UARPin*& OutARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void LoadCloudAnchorByIDAsync_Orphan(class FPendingLatentAction* LatentAction) override;

protected:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (TPair<FString, UAzureCloudSpatialAnchor*>& Pair : CloudAnchorMap)
		{
			Collector.AddReferencedObject(Pair.Value);
		}
	}

	virtual FString GetReferencerName() const override
	{
		return "FAzureSpatialAnchorsForWMR";
	}

private:
	// Map of local AnchorId to CloudAnchor objects.
	TMap<FString, UAzureCloudSpatialAnchor*> CloudAnchorMap;

	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::SaveAsyncDataPtr> SaveAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::DeleteAsyncDataPtr> DeleteAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::LoadByIDAsyncDataPtr> LoadByIDAsyncDataMap;
};
