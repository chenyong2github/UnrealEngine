// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebAPISubsystem.h"

#include "GameDelegates.h"
#include "HttpModule.h"
#include "WebAPIDeveloperSettings.h"
#include "WebAPIOperationObject.h"
#include "Engine/Engine.h"
#include "Security/WebAPIAuthentication.h"

TObjectPtr<UWebAPIOperationObject> FWebAPIPooledOperation::Pop()
{
	if(ensureMsgf(!ItemClass.IsNull(), TEXT("ItemClass %s could not be loaded."), *ItemClass.GetAssetName()))
	{
		// Nothing available, create new
		if(AvailableItems.IsEmpty())
		{
			TObjectPtr<UWebAPIOperationObject> NewItem = NewObject<UWebAPIOperationObject>(GEngine->GetEngineSubsystemBase(UWebAPISubsystem::StaticClass()), ItemClass.LoadSynchronous());
			return ItemsInUse.Add_GetRef(MoveTemp(NewItem));
		}

		// One or more available, so get last
		TObjectPtr<UWebAPIOperationObject> AvailableItem = AvailableItems.Pop(false);

		// Move to in use and return reference 
		return ItemsInUse.Add_GetRef(MoveTemp(AvailableItem));
	}

	return nullptr;
}

bool FWebAPIPooledOperation::Push(const TObjectPtr<UWebAPIOperationObject>& InItem)
{
	check(InItem);
	
#if UE_BUILD_DEBUG
	check(ItemsInUse.Contains(InItem));
#endif

	// Remove from "in use"
	const int32 ItemsRemoved = ItemsInUse.RemoveSwap(InItem, false);

	// Reset to "new" state
	InItem->Reset();

	// Add/return to available
	AvailableItems.Add(InItem);

	return ItemsRemoved > 0;
}

TObjectPtr<UWebAPIOperationObject> UWebAPISubsystem::MakeOperation(const UWebAPIDeveloperSettings* InSettings, const TSubclassOf<UWebAPIOperationObject>& InClass)
{
	check(InClass);

	if(!bUsePooling)
	{
		return NewObject<UWebAPIOperationObject>(this, InClass);		
	}
	
	FName ClassName = InClass->GetFName();
	FWebAPIPooledOperation& NamedOperationPool = OperationPool.FindOrAdd(MoveTemp(ClassName));
	NamedOperationPool.ItemClass = InClass;
	return NamedOperationPool.Pop();
}

void UWebAPISubsystem::ReleaseOperation(const TSubclassOf<UWebAPIOperationObject>& InClass, const TObjectPtr<UWebAPIOperationObject>& InOperation)
{
	check(InClass);

	if(!bUsePooling)
	{
		return;
	}

	FName ClassName = InClass->GetFName();
	FWebAPIPooledOperation& NamedOperationPool = OperationPool.FindOrAdd(MoveTemp(ClassName));
	NamedOperationPool.Push(InOperation);
}

TFuture<TTuple<TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>, bool>> UWebAPISubsystem::MakeHttpRequest(const FString& InVerb, TUniqueFunction<void(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>)> OnRequestCreated) const
{
	check(!InVerb.IsEmpty());
	
	// @todo: buffer requests here and retry after auth

	const TSharedPtr<TPromise<TTuple<FHttpResponsePtr, bool>>, ESPMode::ThreadSafe> Promise = MakeShared<TPromise<TTuple<FHttpResponsePtr, bool>>, ESPMode::ThreadSafe>();

	FHttpModule& HttpModule = FHttpModule::Get();

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule.CreateRequest();
	HttpRequest->SetVerb(InVerb);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[&, Promise](FHttpRequestPtr InRequestPtr, FHttpResponsePtr InResponse, bool bInWasSuccessful)
		{
			Promise->SetValue(MakeTuple(InResponse, bInWasSuccessful));
		});

	// Allows the caller to inject their own Request properties
	OnRequestCreated(HttpRequest);

	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}

void UWebAPISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FGameDelegates::Get().GetEndPlayMapDelegate().AddUObject(this, &UWebAPISubsystem::OnEndPlayMap);
}

void UWebAPISubsystem::Deinitialize()
{
	FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);
	
	Super::Deinitialize();
}

bool UWebAPISubsystem::HandleHttpRequest(TSharedPtr<IHttpRequest> InRequest, UWebAPIDeveloperSettings* InSettings)
{
	const TArray<TSharedPtr<FWebAPIAuthenticationSchemeHandler>>& AuthenticationHandlers = InSettings->GetAuthenticationHandlers();
	for(const TSharedPtr<FWebAPIAuthenticationSchemeHandler>& AuthenticationHandler : AuthenticationHandlers)
	{
		// Returns true if handled, so stop checking other handlers
		if(AuthenticationHandler->HandleHttpRequest(InRequest, InSettings))
		{
			return true;
		}
	}

	return false;
}

bool UWebAPISubsystem::HandleHttpResponse(EHttpResponseCodes::Type InResponseCode, TSharedPtr<IHttpResponse> InResponse, bool bInWasSuccessful, UWebAPIDeveloperSettings* InSettings)
{
	check(InSettings);

	// Run certain codes through auth
	if(InResponseCode == EHttpResponseCodes::Denied)
	{
		const TArray<TSharedPtr<FWebAPIAuthenticationSchemeHandler>>& AuthenticationHandlers = InSettings->GetAuthenticationHandlers();
		for(const TSharedPtr<FWebAPIAuthenticationSchemeHandler>& AuthenticationHandler : AuthenticationHandlers)
		{
			// Returns true if handled, so stop checking other handlers
			if(AuthenticationHandler->HandleHttpResponse(InResponseCode, InResponse, bInWasSuccessful, InSettings))
			{
				break;
			}
		}
	}

	return InSettings->HandleHttpResponse(InResponseCode, InResponse, bInWasSuccessful, InSettings);
}

void UWebAPISubsystem::OnEndPlayMap()
{
	OperationPool.Empty();
}
