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
	virtual bool ConfigSession(const FString& AccountId, const FString& AccountKey, const FCoarseLocalizationSettings& CoarseLocalizationSettings, EAzureSpatialAnchorsLogVerbosity LogVerbosity) override;
	virtual bool StartSession() override;
	virtual void StopSession() override;
	virtual void DestroySession() override;

	virtual bool GetCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) override;

	virtual void GetCloudAnchors(TArray<class UAzureCloudSpatialAnchor*>& OutCloudAnchors) override;
	virtual void GetUnpinnedCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors) override;
	virtual FString GetCloudSpatialAnchorIdentifier(UAzureCloudSpatialAnchor::AzureCloudAnchorID CloudAnchorID) override;
	virtual bool CreateCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) override;

	virtual bool SetCloudAnchorExpiration(const class UAzureCloudSpatialAnchor* const & InCloudAnchor, float Lifetime) override;
	virtual float GetCloudAnchorExpiration(const class UAzureCloudSpatialAnchor* const& InCloudAnchor) override;

	virtual bool SetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor, const TMap<FString, FString>& InAppProperties) override;
	virtual TMap<FString, FString> GetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor) override;

	virtual bool SaveCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool SaveCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void SaveCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool DeleteCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool DeleteCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void DeleteCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool LoadCloudAnchorByIDAsync_Start(class FPendingLatentAction* LatentAction, const FString& InCloudAnchorIdentifier, const FString& InLocalAnchorId, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool LoadCloudAnchorByIDAsync_Update(class FPendingLatentAction* LatentAction, class UARPin*& OutARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void LoadCloudAnchorByIDAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool UpdateCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool UpdateCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void UpdateCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool RefreshCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool RefreshCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void RefreshCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool GetCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual bool GetCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;
	virtual void GetCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) override;

	virtual bool CreateWatcher(const FAzureSpatialAnchorsLocateCriteria& InLocateCriteria, float InWorldToMetersScale, int32& OutWatcherIdentifier, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) override;

	virtual bool StopWatcher(int32 InWatcherIdentifier) override;

	virtual bool CreateARPinAroundAzureCloudSpatialAnchor(const FString& PinId, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, UARPin*& OutARPin) override;

	UAzureCloudSpatialAnchor* GetOrCreateCloudAnchor(AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID);

	void AnchorLocatedCallback(int32 WatcherIdentifier, int32 LocateAnchorStatus, AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID);
	void LocateAnchorsCompletedCallback(int32 WatcherIdentifier, bool WasCanceled);
	void SessionUpdatedCallback(float ReadyForCreateProgress, float RecommendedForCreateProgress, int SessionCreateHash, int SessionLocateHash, int32 SessionUserFeedback);

protected:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (auto anchor : CloudAnchors)
		{
			Collector.AddReferencedObject(anchor);
		}
	}

	virtual FString GetReferencerName() const override
	{
		return "FAzureSpatialAnchorsForWMR";
	}

private:
	void CreateInterop();
	void Reset();

	UAzureCloudSpatialAnchor* GetCloudAnchor(AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID) const;

	TArray<UAzureCloudSpatialAnchor*> CloudAnchors;

	// Maps of the data structs for in-progress async operations.
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::SaveAsyncDataPtr> SaveAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::DeleteAsyncDataPtr> DeleteAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::LoadByIDAsyncDataPtr> LoadByIDAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::UpdateCloudAnchorPropertiesAsyncDataPtr> UpdateCloudAnchorPropertiesAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::RefreshCloudAnchorPropertiesAsyncDataPtr> RefreshCloudAnchorPropertiesAsyncDataMap;
	TMap<class FPendingLatentAction*, AzureSpatialAnchorsInterop::GetCloudAnchorPropertiesAsyncDataPtr> GetCloudAnchorPropertiesAsyncDataMap;
};
