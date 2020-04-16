// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AzureSpatialAnchorsTypes.h"
#include "AzureSpatialAnchors.h"
#include "AzureCloudSpatialAnchor.h"

#include "Features/IModularFeatures.h"
#include "Features/IModularFeature.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Async/TaskGraphInterfaces.h"

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
	virtual bool ConfigSession(const FString& AccountId, const FString& AccountKey, const FCoarseLocalizationSettings& CoarseLocalizationSettings, EAzureSpatialAnchorsLogVerbosity LogVerbosity) = 0;
	virtual bool StartSession() = 0;
	virtual void StopSession() = 0;
	virtual void DestroySession() = 0;

	virtual bool GetCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) = 0;

	virtual void GetCloudAnchors(TArray<class UAzureCloudSpatialAnchor*>& OutCloudAnchors) = 0;
	virtual void GetUnpinnedCloudAnchors(TArray<class UAzureCloudSpatialAnchor*>& OutCloudAnchors) = 0;
	virtual FString GetCloudSpatialAnchorIdentifier(UAzureCloudSpatialAnchor::AzureCloudAnchorID CloudAnchorID) = 0;
	virtual bool CreateCloudAnchor(class UARPin*& InARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor) = 0;

	virtual bool SetCloudAnchorExpiration(const class UAzureCloudSpatialAnchor* const & InCloudAnchor, float Lifetime) = 0;
	virtual float GetCloudAnchorExpiration(const class UAzureCloudSpatialAnchor* const& InCloudAnchor) = 0;

	virtual bool SetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor, const TMap<FString, FString>& InAppProperties) = 0;
	virtual TMap<FString, FString> GetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor) = 0;

	virtual bool SaveCloudAnchorAsync_Start (class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool SaveCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void SaveCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool DeleteCloudAnchorAsync_Start (class FPendingLatentAction* LatentAction, class UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool DeleteCloudAnchorAsync_Update(class FPendingLatentAction* LatentAction, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void DeleteCloudAnchorAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool LoadCloudAnchorByIDAsync_Start(class FPendingLatentAction* LatentAction, const FString& InCloudAnchorIdentifier, const FString& InLocalAnchorId, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool LoadCloudAnchorByIDAsync_Update(class FPendingLatentAction* LatentAction, class UARPin*& OutARPin, class UAzureCloudSpatialAnchor*& OutCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void LoadCloudAnchorByIDAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool UpdateCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool UpdateCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void UpdateCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool RefreshCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool RefreshCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void RefreshCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool GetCloudAnchorPropertiesAsync_Start(class FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual bool GetCloudAnchorPropertiesAsync_Update(class FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;
	virtual void GetCloudAnchorPropertiesAsync_Orphan(class FPendingLatentAction* LatentAction) = 0;

	virtual bool CreateWatcher(const FAzureSpatialAnchorsLocateCriteria& InLocateCriteria, float InWorldToMetersScale, int32& OutWatcherIdentifier, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString) = 0;

	virtual bool StopWatcher(int32 InWatcherIdentifier) = 0;

	virtual bool CreateARPinAroundAzureCloudSpatialAnchor(const FString& PinId, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, UARPin*& OutARPin) = 0;


	/** Delegates that will be cast by the ASA platform implementations. */
	// WatcherIdentifier, status, anchor
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FASAAnchorLocatedDelegate, int32, EAzureSpatialAnchorsLocateAnchorStatus, UAzureCloudSpatialAnchor*);
	static FASAAnchorLocatedDelegate ASAAnchorLocatedDelegate;
	// WatcherIdentifier, canceled
	DECLARE_MULTICAST_DELEGATE_TwoParams(FASALocateAnchorsCompletedDelegate, int32, bool);
	static FASALocateAnchorsCompletedDelegate ASALocateAnchorsCompletedDelegate;
	// ReadyForCreateProgress, RecommendedForCreateProgress, SessionCreateHash, SessionLocateHash, feedback
	DECLARE_MULTICAST_DELEGATE_FiveParams(FASASessionUpdatedDelegate, float, float, int, int, EAzureSpatialAnchorsSessionUserFeedback);
	static FASASessionUpdatedDelegate ASASessionUpdatedDelegate;
	 
	// If an implementation generates events from a thread other than the game thread it should launch these tasks that will fire the delegates on the game
	// thread

	// Note: the anchor located event needs a task as well, but a platform specific implementation is necessary to create the CloudSpatialAnchor so one is not provided here.

	class FASALocateAnchorsCompletedTask
	{
	public:
		FASALocateAnchorsCompletedTask(int32 InWatcherIdentifier, bool InWasCanceled)
			: WatcherIdentifier(InWatcherIdentifier), WasCanceled(InWasCanceled)
		{
		}

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			IAzureSpatialAnchors::ASALocateAnchorsCompletedDelegate.Broadcast(WatcherIdentifier, WasCanceled);
		}

		static FORCEINLINE TStatId GetStatId()
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(ASALocateAnchorsCompletedTask, STATGROUP_TaskGraphTasks);
		}

		static FORCEINLINE ENamedThreads::Type GetDesiredThread()
		{
			return ENamedThreads::GameThread;
		}

		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::FireAndForget;
		}

	private:
		const int32 WatcherIdentifier;
		const bool WasCanceled;
	};

	class FASASessionUpdatedTask
	{
	public:
		FASASessionUpdatedTask(float InReadyForCreateProgress, float InRecommendedForCreateProgress, int InSessionCreateHash, int InSessionLocateHash, EAzureSpatialAnchorsSessionUserFeedback InFeedback)
			: ReadyForCreateProgress(InReadyForCreateProgress), RecommendedForCreateProgress(InRecommendedForCreateProgress), SessionCreateHash(InSessionCreateHash), SessionLocateHash(InSessionLocateHash), Feedback(InFeedback)
		{
		}

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			checkf(CurrentThread == ENamedThreads::GameThread, TEXT("This task can only safely be run on the game thread"));
			IAzureSpatialAnchors::ASASessionUpdatedDelegate.Broadcast(ReadyForCreateProgress, RecommendedForCreateProgress, SessionCreateHash, SessionLocateHash, Feedback);
		}

		static FORCEINLINE TStatId GetStatId()
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(ASASessionUpdatedTask, STATGROUP_TaskGraphTasks);
		}

		static FORCEINLINE ENamedThreads::Type GetDesiredThread()
		{
			return ENamedThreads::GameThread;
		}

		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::FireAndForget;
		}

	private:
		const float ReadyForCreateProgress;
		const float RecommendedForCreateProgress;
		const int SessionCreateHash;
		const int SessionLocateHash;
		const EAzureSpatialAnchorsSessionUserFeedback Feedback;
	};
};
