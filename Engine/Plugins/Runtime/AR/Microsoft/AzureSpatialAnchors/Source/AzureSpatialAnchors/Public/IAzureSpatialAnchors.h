// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Features/IModularFeatures.h"
#include "Features/IModularFeature.h"

#include "AzureSpatialAnchorsTypes.h"
#include "AzureSpatialAnchors.h"

class AZURESPATIALANCHORS_API IAzureSpatialAnchors : public IModularFeature
{
public:
	virtual ~IAzureSpatialAnchors() {}

	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("AzureSpatialAnchors"));
		return FeatureName;
	}

	static inline bool IsAvailable()
	{
		return IModularFeatures::Get().IsModularFeatureAvailable(GetModularFeatureName());
	}

	static inline IAzureSpatialAnchors* Get()
	{
		TArray<IAzureSpatialAnchors*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAzureSpatialAnchors>(GetModularFeatureName());

		// There can be only one!  Or zero.  The implementations are platform specific and we are not currently supporting 'overlapping' platforms.
		check(Impls.Num() <= 1);

		if (Impls.Num() > 0)
		{
			check(Impls[0]);
			return Impls[0];
		}
		return nullptr;
	}

	static void OnLog(const wchar_t* LogMsg)
	{
		UE_LOG(LogAzureSpatialAnchors, Log, TEXT("%s"), LogMsg);
	}

	virtual void StartupModule()
	{
		IModularFeatures::Get().RegisterModularFeature(IAzureSpatialAnchors::GetModularFeatureName(), this);
	}

public:
	virtual bool CreateSession() = 0;
	virtual bool ConfigSession(const FString& AccountId, const FString& AccountKey, EAzureSpatialAnchorsLogVerbosity LogVerbosity) = 0;
	virtual bool StartSession() = 0;
	virtual void StopSession() = 0;
	virtual void DestroySession() = 0;

	virtual bool GetCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) = 0;
	virtual void GetCloudAnchors(TArray<class UAzureCloudSpatialAnchor*>& OutCloudAnchors) = 0;
	virtual bool CreateCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) = 0;
	virtual bool SetCloudAnchorExpiration(class UAzureCloudSpatialAnchor*& InCloudAnchor, int MinutesFromNow) = 0;
	virtual bool SaveCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool SaveCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void SaveCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;
	virtual bool DeleteCloudAnchorAsync_Start(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool DeleteCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void DeleteCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;
	virtual bool LoadCloudAnchorByIDAsync_Start(class FPendingLatentAction* LatentAction, const FString& InCloudAnchorId, const FString& InLocalAnchorId, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool LoadCloudAnchorByIDAsync_Update(class FPendingLatentAction* LatentAction, class UARPin*& OutARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void LoadCloudAnchorByIDAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;
};
