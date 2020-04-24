// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStoreInterfaceGooglePlay.h"
#include "OnlinePurchaseGooglePlay.h"
#include "OnlineAsyncTaskGooglePlayQueryInAppPurchases.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemGooglePlay.h"
#include <jni.h>
#include "Android/AndroidJavaEnv.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"

////////////////////////////////////////////////////////////////////
/// FOnlineStoreGooglePlay implementation

FOnlineStoreGooglePlay::FOnlineStoreGooglePlay(FOnlineSubsystemGooglePlay* InSubsystem)
	: Subsystem( InSubsystem )
	, CurrentQueryTask( nullptr )
{
	UE_LOG_ONLINE_STORE(Display, TEXT( "FOnlineStoreGooglePlay::FOnlineStoreGooglePlay" ));
}

FOnlineStoreGooglePlay::~FOnlineStoreGooglePlay()
{
	UE_LOG_ONLINE_STORE(Display, TEXT( "FOnlineStoreGooglePlay::~FOnlineStoreGooglePlay" ));

	if (Subsystem)
	{
		Subsystem->ClearOnGooglePlayAvailableIAPQueryCompleteDelegate_Handle(AvailableIAPQueryDelegateHandle);
		Subsystem->ClearOnGooglePlayProcessPurchaseCompleteDelegate_Handle(ProcessPurchaseResultDelegateHandle);
		Subsystem->ClearOnGooglePlayRestorePurchasesCompleteDelegate_Handle(RestorePurchasesCompleteDelegateHandle);
	}
}

void FOnlineStoreGooglePlay::Init()
{
	UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGooglePlay::Init"));
	FOnGooglePlayAvailableIAPQueryCompleteDelegate IAPQueryDelegate = FOnGooglePlayAvailableIAPQueryCompleteDelegate::CreateThreadSafeSP(this, &FOnlineStoreGooglePlay::OnGooglePlayAvailableIAPQueryComplete);
	AvailableIAPQueryDelegateHandle = Subsystem->AddOnGooglePlayAvailableIAPQueryCompleteDelegate_Handle(IAPQueryDelegate);

	FOnGooglePlayProcessPurchaseCompleteDelegate PurchaseCompleteDelegate = FOnGooglePlayProcessPurchaseCompleteDelegate::CreateThreadSafeSP(this, &FOnlineStoreGooglePlay::OnProcessPurchaseResult);
	ProcessPurchaseResultDelegateHandle = Subsystem->AddOnGooglePlayProcessPurchaseCompleteDelegate_Handle(PurchaseCompleteDelegate);

	FOnGooglePlayRestorePurchasesCompleteDelegate RestorePurchasesCompleteDelegate = FOnGooglePlayRestorePurchasesCompleteDelegate::CreateThreadSafeSP(this, &FOnlineStoreGooglePlay::OnRestorePurchasesComplete);
	RestorePurchasesCompleteDelegateHandle = Subsystem->AddOnGooglePlayRestorePurchasesCompleteDelegate_Handle(RestorePurchasesCompleteDelegate);

	FString GooglePlayLicenseKey;
	if (!GConfig->GetString(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("GooglePlayLicenseKey"), GooglePlayLicenseKey, GEngineIni) || GooglePlayLicenseKey.IsEmpty())
	{
		UE_LOG_ONLINE_STORE(Warning, TEXT("Missing GooglePlayLicenseKey key in /Script/AndroidRuntimeSettings.AndroidRuntimeSettings of DefaultEngine.ini"));
	}

	extern void AndroidThunkCpp_Iap_SetupIapService(const FString&);
	AndroidThunkCpp_Iap_SetupIapService(GooglePlayLicenseKey);
}

bool FOnlineStoreGooglePlay::IsAllowedToMakePurchases()
{
	UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGooglePlay::IsAllowedToMakePurchases"));

	extern bool AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
	return AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
}


bool FOnlineStoreGooglePlay::QueryForAvailablePurchases(const TArray<FString>& ProductIds, FOnlineProductInformationReadRef& InReadObject)
{
	UE_LOG_ONLINE_STORE(Display, TEXT( "FOnlineStoreGooglePlay::QueryForAvailablePurchases" ));
	
	ReadObject = InReadObject;
	ReadObject->ReadState = EOnlineAsyncTaskState::InProgress;

	CurrentQueryTask = new FOnlineAsyncTaskGooglePlayQueryInAppPurchases(
		Subsystem,
		ProductIds);
	Subsystem->QueueAsyncTask(CurrentQueryTask);

	return true;
}

TSharedRef<FInAppPurchaseProductInfo> ConvertStoreOfferToProduct(const FOnlineStoreOffer& Offer)
{
	TSharedRef<FInAppPurchaseProductInfo> NewProductInfo = MakeShareable(new FInAppPurchaseProductInfo());

	NewProductInfo->Identifier = Offer.OfferId;
	NewProductInfo->DisplayName = Offer.Title.ToString();

	NewProductInfo->DisplayDescription = Offer.Description.ToString(); // Google has only one description, map it to (short) description to match iOS
	NewProductInfo->DisplayPrice = Offer.PriceText.ToString();
	NewProductInfo->CurrencyCode = Offer.CurrencyCode;

	// Convert the backend stated price into its base units
	FInternationalization& I18N = FInternationalization::Get();
	const FCulture& Culture = *I18N.GetCurrentCulture();

	const FDecimalNumberFormattingRules& FormattingRules = Culture.GetCurrencyFormattingRules(NewProductInfo->CurrencyCode);
	const FNumberFormattingOptions& FormattingOptions = FormattingRules.CultureDefaultFormattingOptions;
	double Val = static_cast<double>(Offer.NumericPrice) / static_cast<double>(FMath::Pow(10.0f, FormattingOptions.MaximumFractionalDigits));

	NewProductInfo->RawPrice = Val;

	return NewProductInfo;
}

void FOnlineStoreGooglePlay::OnGooglePlayAvailableIAPQueryComplete(EGooglePlayBillingResponseCode InResponseCode, const TArray<FOnlineStoreOffer>& AvailablePurchases)
{
	UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGooglePlay::OnGooglePlayAvailableIAPQueryComplete"));

	TArray<FInAppPurchaseProductInfo> TempInfos;
	for (const FOnlineStoreOffer& AvailablePurchase : AvailablePurchases)
	{
		TempInfos.Add(*ConvertStoreOfferToProduct(AvailablePurchase));
	}

	if (ReadObject.IsValid())
	{
		ReadObject->ReadState = (InResponseCode == EGooglePlayBillingResponseCode::Ok) ? EOnlineAsyncTaskState::Done : EOnlineAsyncTaskState::Failed;
		ReadObject->ProvidedProductInformation.Insert(TempInfos, 0);
	}

	CurrentQueryTask->ProcessQueryAvailablePurchasesResults(InResponseCode == EGooglePlayBillingResponseCode::Ok);

	// clear the pointer, it will be destroyed by the async task manager
	CurrentQueryTask = nullptr;
}

bool FOnlineStoreGooglePlay::BeginPurchase(const FInAppPurchaseProductRequest& ProductRequest, FOnlineInAppPurchaseTransactionRef& InPurchaseStateObject)
{
	UE_LOG_ONLINE_STORE(Display, TEXT( "FOnlineStoreGooglePlay::BeginPurchase" ));
	
	bool bCreatedNewTransaction = false;
	
	if (IsAllowedToMakePurchases())
	{
		CachedPurchaseStateObject = InPurchaseStateObject;
		CachedPurchaseStateObject->bIsConsumable = ProductRequest.bIsConsumable;

		extern bool AndroidThunkCpp_Iap_BeginPurchase(const FString&);
		bCreatedNewTransaction = AndroidThunkCpp_Iap_BeginPurchase(ProductRequest.ProductIdentifier);
		UE_LOG_ONLINE_STORE(Display, TEXT("Created Transaction? - %s"), 
			bCreatedNewTransaction ? TEXT("Created a transaction.") : TEXT("Failed to create a transaction."));

		if (!bCreatedNewTransaction)
		{
			UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGooglePlay::BeginPurchase - Could not create a new transaction."));
			CachedPurchaseStateObject->ReadState = EOnlineAsyncTaskState::Failed;
			TriggerOnInAppPurchaseCompleteDelegates(EInAppPurchaseState::Invalid);
		}
		else
		{
			CachedPurchaseStateObject->ReadState = EOnlineAsyncTaskState::InProgress;
		}
	}
	else
	{
		UE_LOG_ONLINE_STORE(Display, TEXT("This device is not able to make purchases."));

		InPurchaseStateObject->ReadState = EOnlineAsyncTaskState::Failed;
		TriggerOnInAppPurchaseCompleteDelegates(EInAppPurchaseState::NotAllowed);
	}

	return bCreatedNewTransaction;
}

void FOnlineStoreGooglePlay::OnProcessPurchaseResult(EGooglePlayBillingResponseCode InResponseCode, const FGoogleTransactionData& InTransactionData)
{
	UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGooglePlay::OnProcessPurchaseResult"));
	UE_LOG_ONLINE_STORE(Display, TEXT("3... Response: %s Transaction: %s"), ToString(InResponseCode), *InTransactionData.ToDebugString());

	bool bWasSuccessful = (InResponseCode == EGooglePlayBillingResponseCode::Ok);
	if (CachedPurchaseStateObject.IsValid())
	{
		if (CachedPurchaseStateObject->bIsConsumable && InTransactionData.GetErrorStr().IsEmpty())
		{
			// Consume right away to maintain behavior of legacy code (GooglePlayStoreHelper.java)
			// Technically wrong/dangerous because the game may not grant entitlements
			// on a crash or other interruption 
			extern void AndroidThunkCpp_Iap_ConsumePurchase(const FString&);
			AndroidThunkCpp_Iap_ConsumePurchase(InTransactionData.GetTransactionIdentifier());
		}

		FInAppPurchaseProductInfo& ProductInfo = CachedPurchaseStateObject->ProvidedProductInformation;
		ProductInfo.Identifier = InTransactionData.GetOfferId();
		ProductInfo.DisplayName = TEXT("n/a");
		ProductInfo.DisplayDescription = TEXT("n/a");
		ProductInfo.DisplayPrice = TEXT("n/a");
		ProductInfo.ReceiptData = InTransactionData.GetCombinedReceiptData();
		ProductInfo.TransactionIdentifier = InTransactionData.GetTransactionIdentifier();

		CachedPurchaseStateObject->ReadState = bWasSuccessful ? EOnlineAsyncTaskState::Done : EOnlineAsyncTaskState::Failed;
	}

	TriggerOnInAppPurchaseCompleteDelegates(ConvertGPResponseCodeToIAPState(InResponseCode));
}


bool FOnlineStoreGooglePlay::RestorePurchases(const TArray<FInAppPurchaseProductRequest>& ConsumableProductFlags, FOnlineInAppPurchaseRestoreReadRef& InReadObject)
{
	bool bSentAQueryRequest = false;
	CachedPurchaseRestoreObject = InReadObject;

	if (IsAllowedToMakePurchases())
	{
		TArray<FString> ProductIds;
		TArray<bool> IsConsumableFlags;

		for (int i = 0; i < ConsumableProductFlags.Num(); i++)
		{
			ProductIds.Add(ConsumableProductFlags[i].ProductIdentifier);
			IsConsumableFlags.Add(ConsumableProductFlags[i].bIsConsumable);
		}

		// Send JNI request
		extern bool AndroidThunkCpp_Iap_RestorePurchases(const TArray<FString>&, const TArray<bool>&);
		bSentAQueryRequest = AndroidThunkCpp_Iap_RestorePurchases(ProductIds, IsConsumableFlags);
	}
	else
	{
		UE_LOG_ONLINE_STORE(Display, TEXT("This device is not able to make purchases."));
		TriggerOnInAppPurchaseRestoreCompleteDelegates(EInAppPurchaseState::NotAllowed);
	}

	return bSentAQueryRequest;
}

void FOnlineStoreGooglePlay::OnRestorePurchasesComplete(EGooglePlayBillingResponseCode InResponseCode, const TArray<FGoogleTransactionData>& InRestoredPurchases)
{
	UE_LOG_ONLINE_STORE(Verbose, TEXT("FOnlineStoreGooglePlay::OnRestorePurchasesComplete Response: %s Num: %d"), ToString(InResponseCode), InRestoredPurchases.Num());

	bool bWasSuccessful = (InResponseCode == EGooglePlayBillingResponseCode::Ok);
	if (CachedPurchaseRestoreObject.IsValid())
	{
		TArray<FInAppPurchaseRestoreInfo> RestoredPurchaseInfo;
		RestoredPurchaseInfo.Reserve(InRestoredPurchases.Num());
		for (const FGoogleTransactionData& RestoredPurchase : InRestoredPurchases)
		{
			FInAppPurchaseRestoreInfo RestoreInfo;
			RestoreInfo.Identifier = RestoredPurchase.GetOfferId();
			RestoreInfo.ReceiptData = RestoredPurchase.GetCombinedReceiptData();
			RestoreInfo.TransactionIdentifier = RestoredPurchase.GetTransactionIdentifier();
			RestoredPurchaseInfo.Add(RestoreInfo);
		}

		CachedPurchaseRestoreObject->ProvidedRestoreInformation = RestoredPurchaseInfo;
		CachedPurchaseRestoreObject->ReadState = bWasSuccessful ? EOnlineAsyncTaskState::Done : EOnlineAsyncTaskState::Failed;
	}

	EInAppPurchaseState::Type IAPState = bWasSuccessful ? EInAppPurchaseState::Restored : ConvertGPResponseCodeToIAPState(InResponseCode);

	TriggerOnInAppPurchaseRestoreCompleteDelegates(IAPState);
}

