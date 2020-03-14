// Copyright Epic Games, Inc. All Rights Reserved.

#include "AzureSpatialAnchorsFunctionLibrary.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
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

bool UAzureSpatialAnchorsLibrary::ConfigSession(const FString& AccountId, const FString& AccountKey, const FCoarseLocalizationSettings CoarseLocalizationSettings, EAzureSpatialAnchorsLogVerbosity LogVerbosity)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->ConfigSession(AccountId, AccountKey, CoarseLocalizationSettings, LogVerbosity);
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

struct FAzureSpatialAnchorsAsyncAction : public FPendingLatentAction
{
public:
	FAzureSpatialAnchorsAsyncAction(const TCHAR* InDescription, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FPendingLatentAction()
		, OutResult(InOutResult)
		, OutErrorString(InOutErrorString)
		, Description(InDescription)
	{}

	virtual void NotifyObjectDestroyed() override
	{
		Orphan();
	}

	virtual void NotifyActionAborted() override
	{
		Orphan();
	}

	virtual void Orphan() = 0;

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return Description;
	}
#endif

protected:
	EAzureSpatialAnchorsResult& OutResult;
	FString& OutErrorString;

private:
	FString Description;
};

struct FAzureSpatialAnchorsSavePinToCloudAction : public FAzureSpatialAnchorsAsyncAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UARPin* ARPin;
	float Lifetime;
	UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor;

	FAzureSpatialAnchorsSavePinToCloudAction(const FLatentActionInfo& InLatentInfo, UARPin*& InARPin, float InLifetime, UAzureCloudSpatialAnchor*& InOutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("SavePinToCloud."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, ARPin(InARPin)
		, Lifetime(InLifetime)
		, OutAzureCloudSpatialAnchor(InOutAzureCloudSpatialAnchor)
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

			if (Lifetime > 0)
			{
				if (!MSA->SetCloudAnchorExpiration(OutAzureCloudSpatialAnchor, Lifetime))
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

	virtual void Orphan() override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->SaveCloudAnchorAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::SavePinToCloud(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UARPin* ARPin, float Lifetime, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("SavePinToCloud Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsSavePinToCloudAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsSavePinToCloudAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsSavePinToCloudAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->ARPin != ARPin)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsSavePinToCloudAction* NewAction = new FAzureSpatialAnchorsSavePinToCloudAction(LatentInfo, ARPin, Lifetime, OutAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping SavePinToCloud latent action."), LatentInfo.UUID);
		}
	}
}

struct FAzureSpatialAnchorsDeleteCloudAnchorAction : public FAzureSpatialAnchorsAsyncAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UAzureCloudSpatialAnchor* CloudSpatialAnchor;

	FAzureSpatialAnchorsDeleteCloudAnchorAction(const FLatentActionInfo& InLatentInfo, UAzureCloudSpatialAnchor* InCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("DeleteCloudAnchor."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, CloudSpatialAnchor(InCloudSpatialAnchor)
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

	virtual void Orphan() override
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

struct FAzureSpatialAnchorsLoadCloudAnchorAction : public FAzureSpatialAnchorsAsyncAction
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

	FAzureSpatialAnchorsLoadCloudAnchorAction(const FLatentActionInfo& InLatentInfo, FString InCloudId, FString InPinId, UARPin*& InOutARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("LoadCloudAnchor."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, PinId(InPinId)
		, CloudId(InCloudId)
		, OutARPin(InOutARPin)
		, OutAzureCloudSpatialAnchor(OutAzureCloudSpatialAnchor)
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

	virtual void Orphan() override
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

void UAzureSpatialAnchorsLibrary::CreateCloudAnchor(UARPin* ARPin, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return;
	}

	if (MSA->CreateCloudAnchor(ARPin, OutAzureCloudSpatialAnchor))
	{
		OutResult = EAzureSpatialAnchorsResult::Success;
	}
	else
	{
		OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
		OutErrorString = TEXT("CreateCloudAnchor Failed.");
	}
}

struct FAzureSpatialAnchorsSaveCloudAnchorAction : public FAzureSpatialAnchorsAsyncAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor;

	FAzureSpatialAnchorsSaveCloudAnchorAction(const FLatentActionInfo& InLatentInfo, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("SaveCloudAnchor."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, InAzureCloudSpatialAnchor(InAzureCloudSpatialAnchor)
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
			if (!MSA->SaveCloudAnchorAsync_Start(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
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
			if (MSA->SaveCloudAnchorAsync_Update(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		}
	}

	virtual void Orphan() override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->SaveCloudAnchorAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::SaveCloudAnchor(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("SaveCloudAnchor Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsSaveCloudAnchorAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsSaveCloudAnchorAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsSaveCloudAnchorAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->InAzureCloudSpatialAnchor != InAzureCloudSpatialAnchor)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsSaveCloudAnchorAction* NewAction = new FAzureSpatialAnchorsSaveCloudAnchorAction(LatentInfo, InAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping SaveCloudAnchor latent action."), LatentInfo.UUID);
		}
	}
}

struct FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction : public FAzureSpatialAnchorsAsyncAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor;

	FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction(const FLatentActionInfo& InLatentInfo, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("UpdateCloudAnchorProperties."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, InAzureCloudSpatialAnchor(InAzureCloudSpatialAnchor)
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
			if (!MSA->UpdateCloudAnchorPropertiesAsync_Start(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
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
			if (MSA->UpdateCloudAnchorPropertiesAsync_Update(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		}
	}

	virtual void Orphan() override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->UpdateCloudAnchorPropertiesAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::UpdateCloudAnchorProperties(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("UpdateCloudAnchorProperties Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->InAzureCloudSpatialAnchor != InAzureCloudSpatialAnchor)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction* NewAction = new FAzureSpatialAnchorsUpdateCloudAnchorPropertiesAction(LatentInfo, InAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping UpdateCloudAnchorProperties latent action."), LatentInfo.UUID);
		}
	}
}

struct FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction : public FAzureSpatialAnchorsAsyncAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bStarted;

	UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor;

	FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction(const FLatentActionInfo& InLatentInfo, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
		: FAzureSpatialAnchorsAsyncAction(TEXT("RefreshCloudAnchorProperties."), InOutResult, InOutErrorString)
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
		, bStarted(false)
		, InAzureCloudSpatialAnchor(InAzureCloudSpatialAnchor)
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
			if (!MSA->RefreshCloudAnchorPropertiesAsync_Start(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
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
			if (MSA->RefreshCloudAnchorPropertiesAsync_Update(this, InAzureCloudSpatialAnchor, OutResult, OutErrorString))
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
				return;
			}
		}
	}

	virtual void Orphan() override
	{
		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA)
		{
			MSA->RefreshCloudAnchorPropertiesAsync_Orphan(this);
		}
	}
};

void UAzureSpatialAnchorsLibrary::RefreshCloudAnchorProperties(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("RefreshCloudAnchorProperties Action. UUID: %d"), LatentInfo.UUID);
		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction*>(
			LatentManager.FindExistingAction<FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
		if (ExistAction == nullptr || ExistAction->InAzureCloudSpatialAnchor != InAzureCloudSpatialAnchor)
		{
			// does this handle multiple in progress operations?
			FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction* NewAction = new FAzureSpatialAnchorsRefreshCloudAnchorPropertiesAction(LatentInfo, InAzureCloudSpatialAnchor, OutResult, OutErrorString);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping RefreshCloudAnchorProperties latent action."), LatentInfo.UUID);
		}
	}
}

//struct FAzureSpatialAnchorsGetCloudAnchorPropertiesAction : public FAzureSpatialAnchorsAsyncAction
//{
//public:
//	FName ExecutionFunction;
//	int32 OutputLink;
//	FWeakObjectPtr CallbackTarget;
//	bool bStarted;
//
//	FString CloudIdentifier;
//	UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor;
//
//	FAzureSpatialAnchorsGetCloudAnchorPropertiesAction(const FLatentActionInfo& InLatentInfo, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& InOutResult, FString& InOutErrorString)
//		: FAzureSpatialAnchorsAsyncAction(TEXT("GetCloudAnchorProperties."), InOutResult, InOutErrorString)
//		, ExecutionFunction(InLatentInfo.ExecutionFunction)
//		, OutputLink(InLatentInfo.Linkage)
//		, CallbackTarget(InLatentInfo.CallbackTarget)
//		, bStarted(false)
//		, CloudIdentifier(CloudIdentifier)
//		, OutAzureCloudSpatialAnchor(OutAzureCloudSpatialAnchor)
//	{}
//
//	virtual void UpdateOperation(FLatentResponse& Response) override
//	{
//		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
//		if (MSA == nullptr)
//		{
//			OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
//			OutErrorString = TEXT("Failed to get IAzureSpatialAnchors.");
//			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
//			return;
//		}
//
//		if (!bStarted)
//		{
//			// Start the operation	
//			if (!MSA->GetCloudAnchorPropertiesAsync_Start(this, CloudIdentifier, OutAzureCloudSpatialAnchor, OutResult, OutErrorString))
//			{
//				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
//				return;
//			}
//			else
//			{
//				bStarted = true;
//			}
//		}
//		else
//		{
//			// See if the operation is done.
//			if (MSA->GetCloudAnchorPropertiesAsync_Update(this, CloudIdentifier, OutAzureCloudSpatialAnchor, OutResult, OutErrorString))
//			{
//				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
//				return;
//			}
//		}
//	}
//
//	virtual void Orphan() override
//	{
//		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
//		if (MSA)
//		{
//			MSA->GetCloudAnchorPropertiesAsync_Orphan(this);
//		}
//	}
//};
//
//void UAzureSpatialAnchorsLibrary::GetCloudAnchorProperties(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
//{
//	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
//	{
//		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("GetCloudAnchorProperties Action. UUID: %d"), LatentInfo.UUID);
//		FLatentActionManager& LatentManager = World->GetLatentActionManager();
//
//		FAzureSpatialAnchorsGetCloudAnchorPropertiesAction* ExistAction = reinterpret_cast<FAzureSpatialAnchorsGetCloudAnchorPropertiesAction*>(
//			LatentManager.FindExistingAction<FAzureSpatialAnchorsGetCloudAnchorPropertiesAction>(LatentInfo.CallbackTarget, LatentInfo.UUID));
//		if (ExistAction == nullptr || ExistAction->CloudIdentifier != CloudIdentifier)
//		{
//			// does this handle multiple in progress operations?
//			FAzureSpatialAnchorsGetCloudAnchorPropertiesAction* NewAction = new FAzureSpatialAnchorsGetCloudAnchorPropertiesAction(LatentInfo, CloudIdentifier, OutAzureCloudSpatialAnchor, OutResult, OutErrorString);
//			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
//		}
//		else
//		{
//			UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("Skipping GetCloudAnchorProperties latent action."), LatentInfo.UUID);
//		}
//	}
//}

void UAzureSpatialAnchorsLibrary::CreateWatcher(UObject* WorldContextObject, const FAzureSpatialAnchorsLocateCriteria& InLocateCriteria, int32& OutWatcherIdentifier, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UE_LOG(LogAzureSpatialAnchors, Verbose, TEXT("CreateWatcher"));

		IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
		if (MSA == nullptr)
		{
			OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
			OutErrorString = TEXT("Failed to get IAzureSpatialAnchors.");
			return;
		}

		const float WorldToMetersScale = WorldContextObject->GetWorld()->GetWorldSettings()->WorldToMeters;
		MSA->CreateWatcher(InLocateCriteria, WorldToMetersScale, OutWatcherIdentifier, OutResult, OutErrorString);
	}
	else
	{
		OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
		OutErrorString = TEXT("Failed to get World from WorldContextObject.");
	}
}

bool UAzureSpatialAnchorsLibrary::StopWatcher(int32 InWatcherIdentifier)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->StopWatcher(InWatcherIdentifier);
}

bool UAzureSpatialAnchorsLibrary::CreateARPinAroundAzureCloudSpatialAnchor(FString PinId, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, UARPin*& OutARPin)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return false;
	}

	return MSA->CreateARPinAroundAzureCloudSpatialAnchor(PinId, InAzureCloudSpatialAnchor, OutARPin);
}



