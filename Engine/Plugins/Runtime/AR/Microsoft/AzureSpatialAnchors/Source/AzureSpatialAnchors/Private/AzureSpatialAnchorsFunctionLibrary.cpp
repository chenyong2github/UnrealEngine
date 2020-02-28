// Copyright Epic Games, Inc. All Rights Reserved.

#include "AzureSpatialAnchorsFunctionLibrary.h"
#include "Engine/Engine.h"
#include "IAzureSpatialAnchors.h"

#include "ARPin.h"
#include "LatentActions.h"

bool UAzureSpatialAnchorsLibrary::CreateSession()
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->CreateSession();
}

bool UAzureSpatialAnchorsLibrary::ConfigSession(const FString& AccountId, const FString& AccountKey, EAzureSpatialAnchorsLogVerbosity LogVerbosity)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->ConfigSession(AccountId, AccountKey, LogVerbosity);
}

bool UAzureSpatialAnchorsLibrary::StartSession()
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->StartSession();
}

bool UAzureSpatialAnchorsLibrary::StopSession()
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	MSA->StopSession();
	return true;
}

bool UAzureSpatialAnchorsLibrary::DestroySession()
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	MSA->DestroySession();
	return true;
}

void UAzureSpatialAnchorsLibrary::GetCloudAnchor(UARPin* ARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		OutAzureCloudSpatialAnchor = nullptr;
		return;
	}

	MSA->GetCloudAnchor(ARPin, OutAzureCloudSpatialAnchor);
}

void UAzureSpatialAnchorsLibrary::GetCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		OutCloudAnchors.Empty();
		return;
	}

	MSA->GetCloudAnchors(OutCloudAnchors);
}

struct FAzureSpatialAnchorsSavePinToAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UARPin* ARPin;
	int MinutesFromNow;
	UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor;
	EAzureSpatialAnchorsResult& OutResult;
	FString& OutErrorString;

	FAzureSpatialAnchorsSavePinToAction(const FLatentActionInfo& InLatentInfo, UARPin*& InARPin, int InMinutesFromNow, UAzureCloudSpatialAnchor*& InOutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FPendingLatentAction()
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, ARPin(InARPin)
		, MinutesFromNow(InMinutesFromNow)
		, OutAzureCloudSpatialAnchor(InOutAzureCloudSpatialAnchor)
		, OutResult(InOutResult)
		, OutErrorString(InOutErrorString)
	{}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA == nullptr)
		{
			OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
			OutErrorString = TEXT("Failed to get IAzureSpatialAnchors.");
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
			return;
		}

		if (!bStarted)
		{
			// Start the operation
			if (!MSA->CreateCloudAnchor(ARPin, OutAzureCloudSpatialAnchor))
			{
				OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
				OutErrorString = TEXT("CreateCloudAnchor Failed.");
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}

			if (MinutesFromNow > 0)
			{
				if (!MSA->SetCloudAnchorExpiration(OutAzureCloudSpatialAnchor, MinutesFromNow))
				{
					OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
					OutErrorString = TEXT("SetCloudAnchorExpiration Failed.");
					Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
					return;
				}
			}
			
			if (!MSA->SaveCloudAnchorAsync_Start(this, OutAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
			else
			{
				bStarted = true;
			}
		}
		else
		{
			// See if the operation is done.
			if (MSA->SaveCloudAnchorAsync_Update(this, OutAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		}
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("SavePinToCloud."));
	}
#endif

	virtual void NotifyObjectDestroyed() 
	{
		Orphan();
	}

	virtual void NotifyActionAborted() 
	{
		Orphan();
	}

	void Orphan()
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->SaveCloudAnchorAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::SavePinToCloud(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UARPin* ARPin, int InMinutesFromNow, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("SavePinToCloud Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsSavePinToAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsSavePinToAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsSavePinToAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->ARPin != ARPin)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsSavePinToAction* NewAction = new FAzureSpatialAnchorsSavePinToAction(LatentInfo, ARPin, InMinutesFromNow, OutAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping SavePinToCloud latent action."), LatentInfo.UUID);
		}
	}
}

struct FAzureSpatialAnchorsDeleteCloudAnchorAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UAzureCloudSpatialAnchor*& CloudSpatialAnchor;
	EAzureSpatialAnchorsResult& OutResult;
	FString& OutErrorString;

	FAzureSpatialAnchorsDeleteCloudAnchorAction(const FLatentActionInfo& InLatentInfo, UAzureCloudSpatialAnchor*& InCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FPendingLatentAction()
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, CloudSpatialAnchor(InCloudSpatialAnchor)
		, OutResult(InOutResult)
		, OutErrorString(InOutErrorString)
	{}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA == nullptr)
		{
			OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
			OutErrorString = TEXT("Failed to get IAzureSpatialAnchors.");
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
			return;
		}

		if (!bStarted)
		{
			// Start the operation
			if (!MSA->DeleteCloudAnchorAsync_Start(this, CloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
			else
			{
				bStarted = true;
			}
		}
		else
		{
			// See if the operation is done.
			if (MSA->DeleteCloudAnchorAsync_Update(this, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		} 
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("SavePinToCloud."));
	}
#endif

	virtual void NotifyObjectDestroyed()
	{
		Orphan();
	}

	virtual void NotifyActionAborted()
	{
		Orphan();
	}

	void Orphan()
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->DeleteCloudAnchorAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::DeleteCloudAnchor(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UAzureCloudSpatialAnchor* InCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("DeleteCloudAnchor Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsDeleteCloudAnchorAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsDeleteCloudAnchorAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsDeleteCloudAnchorAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->CloudSpatialAnchor != InCloudSpatialAnchor)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsDeleteCloudAnchorAction* NewAction = new FAzureSpatialAnchorsDeleteCloudAnchorAction(LatentInfo, InCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping DeleteCloudAnchor latent action."), LatentInfo.UUID);
		}
	}
}

struct FAzureSpatialAnchorsLoadCloudAnchorAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	FString PinId;
	FString CloudId;
	UARPin*& OutARPin;
	UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor;
	EAzureSpatialAnchorsResult& OutResult;
	FString& OutErrorString;

	FAzureSpatialAnchorsLoadCloudAnchorAction(const FLatentActionInfo& InLatentInfo, FString InCloudId, FString InPinId, UARPin*& InOutARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FPendingLatentAction()
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, PinId(InPinId)
		, CloudId(InCloudId)
		, OutARPin(InOutARPin)
		, OutAzureCloudSpatialAnchor(OutAzureCloudSpatialAnchor)
		, OutResult(InOutResult)
		, OutErrorString(InOutErrorString)
	{}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA == nullptr)
		{
			OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
			OutErrorString = TEXT("Failed to get IAzureSpatialAnchors.");
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
			return;
		}

		if (!bStarted)
		{
			// Start the operation
			if (CloudId.IsEmpty())
			{
				OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
				OutErrorString = TEXT("InCloudId is empty.  No load attempted.");
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}

			if (!MSA->LoadCloudAnchorByIDAsync_Start(this, *CloudId, *PinId, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
			else
			{
				bStarted = true;
			}
		}
		else
		{
			// See if the operation is done.
			if (MSA->LoadCloudAnchorByIDAsync_Update(this, OutARPin, OutAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		}
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("SavePinToCloud."));
	}
#endif

	virtual void NotifyObjectDestroyed()
	{
		Orphan();
	}

	virtual void NotifyActionAborted()
	{
		Orphan();
	}

	void Orphan()
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->LoadCloudAnchorByIDAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::LoadCloudAnchor(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, FString CloudId, FString PinId, UARPin*& OutARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("LoadCloudAnchor Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsLoadCloudAnchorAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsLoadCloudAnchorAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsLoadCloudAnchorAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->CloudId != CloudId)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsLoadCloudAnchorAction* NewAction = new FAzureSpatialAnchorsLoadCloudAnchorAction(LatentInfo, CloudId, PinId, OutARPin, OutAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping LoadCloudAnchor latent action."), LatentInfo.UUID);
		}
	}
}





