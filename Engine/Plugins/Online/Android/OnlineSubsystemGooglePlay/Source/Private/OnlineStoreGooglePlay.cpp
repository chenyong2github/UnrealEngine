// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStoreGooglePlay.h"
#include "OnlineSubsystemGooglePlay.h"
#include "OnlineAsyncTaskGooglePlayQueryInAppPurchases.h"

#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Misc/ConfigCacheIni.h"
#include <jni.h>
#include "Android/AndroidJavaEnv.h"
#include "Async/TaskGraphInterfaces.h"

FOnlineStoreGooglePlayV2::FOnlineStoreGooglePlayV2(FOnlineSubsystemGooglePlay* InSubsystem)
	: bIsQueryInFlight(false)
	, Subsystem(InSubsystem)
{
	UE_LOG_ONLINE_STOREV2(Verbose, TEXT( "FOnlineStoreGooglePlayV2::FOnlineStoreGooglePlayV2" ));
}

FOnlineStoreGooglePlayV2::FOnlineStoreGooglePlayV2()
	: bIsQueryInFlight(false)
	, Subsystem(nullptr)
{
	UE_LOG_ONLINE_STOREV2(Verbose, TEXT( "FOnlineStoreGooglePlayV2::FOnlineStoreGooglePlayV2 empty" ));
}

FOnlineStoreGooglePlayV2::~FOnlineStoreGooglePlayV2()
{
	if (Subsystem)
	{
		Subsystem->ClearOnGooglePlayAvailableIAPQueryCompleteDelegate_Handle(AvailableIAPQueryDelegateHandle);
	}
}

void FOnlineStoreGooglePlayV2::Init()
{
	UE_LOG_ONLINE_STOREV2(Verbose, TEXT("FOnlineStoreGooglePlayV2::Init"));

	FOnGooglePlayAvailableIAPQueryCompleteDelegate Delegate = FOnGooglePlayAvailableIAPQueryCompleteDelegate::CreateThreadSafeSP(this, &FOnlineStoreGooglePlayV2::OnGooglePlayAvailableIAPQueryComplete);
	AvailableIAPQueryDelegateHandle = Subsystem->AddOnGooglePlayAvailableIAPQueryCompleteDelegate_Handle(Delegate);

	FString GooglePlayLicenseKey;
	if (!GConfig->GetString(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("GooglePlayLicenseKey"), GooglePlayLicenseKey, GEngineIni) || GooglePlayLicenseKey.IsEmpty())
	{
		UE_LOG_ONLINE_STOREV2(Warning, TEXT("Missing GooglePlayLicenseKey key in /Script/AndroidRuntimeSettings.AndroidRuntimeSettings of DefaultEngine.ini"));
	}

	extern void AndroidThunkCpp_Iap_SetupIapService(const FString&);
	AndroidThunkCpp_Iap_SetupIapService(GooglePlayLicenseKey);
}

#if OSSGOOGLEPLAY_WITH_AIDL
TSharedRef<FOnlineStoreOffer> ConvertProductToStoreOffer(const FInAppPurchaseProductInfo& Product)
{
	TSharedRef<FOnlineStoreOffer> NewProductInfo = MakeShareable(new FOnlineStoreOffer());

	NewProductInfo->OfferId = Product.Identifier;

	FString Title = Product.DisplayName;
	int32 OpenParenIdx = -1;
	int32 CloseParenIdx = -1;
	if (Title.FindLastChar(TEXT(')'), CloseParenIdx) && Title.FindLastChar(TEXT('('), OpenParenIdx) && (OpenParenIdx < CloseParenIdx))
	{
		Title.LeftInline(OpenParenIdx);
		Title.TrimEndInline();
	}

	NewProductInfo->Title = FText::FromString(Title);
	NewProductInfo->Description = FText::FromString(Product.DisplayDescription); // Google has only one description, map it to (short) description to match iOS
	//NewProductInfo->LongDescription = FText::FromString(Product.DisplayDescription); // leave this empty so we know it's not set (client can apply more info from MCP)
	NewProductInfo->PriceText = FText::FromString(Product.DisplayPrice);
	NewProductInfo->CurrencyCode = Product.CurrencyCode;

	// Convert the backend stated price into its base units
	FInternationalization& I18N = FInternationalization::Get();
	const FCulture& Culture = *I18N.GetCurrentCulture();

	const FDecimalNumberFormattingRules& FormattingRules = Culture.GetCurrencyFormattingRules(NewProductInfo->CurrencyCode);
	const FNumberFormattingOptions& FormattingOptions = FormattingRules.CultureDefaultFormattingOptions;
	double Val = static_cast<double>(Product.RawPrice) * static_cast<double>(FMath::Pow(10.0f, FormattingOptions.MaximumFractionalDigits));

	NewProductInfo->NumericPrice = FMath::TruncToInt(Val + 0.5);

	// Google doesn't support these fields, set to min and max defaults
	NewProductInfo->ReleaseDate = FDateTime::MinValue();
	NewProductInfo->ExpirationDate = FDateTime::MaxValue();

	return NewProductInfo;
}
#else
TSharedRef<FOnlineStoreOffer> ConvertProductToStoreOffer(const FOnlineStoreOffer& Product)
{
	return MakeShared<FOnlineStoreOffer>(Product);
}
#endif

void FOnlineStoreGooglePlayV2::OnGooglePlayAvailableIAPQueryComplete(EGooglePlayBillingResponseCode InResponseCode, const TArray<FProvidedProductInformation>& InProvidedProductInformation)
{ 
	TArray<FUniqueOfferId> OfferIds;
	for (const FProvidedProductInformation& Product : InProvidedProductInformation)
	{
		TSharedRef<FOnlineStoreOffer> NewProductOffer = ConvertProductToStoreOffer(Product);

		AddOffer(NewProductOffer);
		OfferIds.Add(NewProductOffer->OfferId);

		UE_LOG_ONLINE_STOREV2(Log, TEXT("Product Identifier: %s, Name: %s, Desc: %s, Long Desc: %s, Price: %s IntPrice: %d"),
			*NewProductOffer->OfferId,
			*NewProductOffer->Title.ToString(),
			*NewProductOffer->Description.ToString(),
			*NewProductOffer->LongDescription.ToString(),
			*NewProductOffer->PriceText.ToString(),
			NewProductOffer->NumericPrice);
	}

	if (CurrentQueryTask)
	{
		CurrentQueryTask->ProcessQueryAvailablePurchasesResults(InResponseCode);

		// clear the pointer, it will be destroyed by the async task manager
		CurrentQueryTask = nullptr;
	}
	else
	{
		UE_LOG_ONLINE_STOREV2(Log, TEXT("OnGooglePlayAvailableIAPQueryComplete: No IAP query task in flight"));
	}

	bIsQueryInFlight = false;
}

void FOnlineStoreGooglePlayV2::QueryCategories(const FUniqueNetId& UserId, const FOnQueryOnlineStoreCategoriesComplete& Delegate)
{
	Delegate.ExecuteIfBound(false, TEXT("No CatalogService"));
}

void FOnlineStoreGooglePlayV2::GetCategories(TArray<FOnlineStoreCategory>& OutCategories) const
{
	OutCategories.Empty();
}

void FOnlineStoreGooglePlayV2::QueryOffersByFilter(const FUniqueNetId& UserId, const FOnlineStoreFilter& Filter, const FOnQueryOnlineStoreOffersComplete& Delegate)
{
	Delegate.ExecuteIfBound(false, TArray<FUniqueOfferId>(), TEXT("No CatalogService"));
}

void FOnlineStoreGooglePlayV2::QueryOffersById(const FUniqueNetId& UserId, const TArray<FUniqueOfferId>& OfferIds, const FOnQueryOnlineStoreOffersComplete& Delegate)
{
	UE_LOG_ONLINE_STOREV2(Verbose, TEXT("FOnlineStoreGooglePlayV2::QueryOffersById"));

	if (bIsQueryInFlight)
	{
		Delegate.ExecuteIfBound(false, OfferIds, TEXT("Request already in flight"));
	}
	else if (OfferIds.Num() == 0)
	{
		Delegate.ExecuteIfBound(false, OfferIds, TEXT("No offers to query for"));
	}
	else
	{
	CurrentQueryTask = new FOnlineAsyncTaskGooglePlayQueryInAppPurchasesV2(
		Subsystem,
		OfferIds,
		Delegate);
	Subsystem->QueueAsyncTask(CurrentQueryTask);

		bIsQueryInFlight = true;
	}
}

void FOnlineStoreGooglePlayV2::AddOffer(const TSharedRef<FOnlineStoreOffer>& NewOffer)
{
	TSharedRef<FOnlineStoreOffer>* Existing = CachedOffers.Find(NewOffer->OfferId);
	if (Existing != nullptr)
	{
		// Replace existing offer
		*Existing = NewOffer;
	}
	else
	{
		CachedOffers.Add(NewOffer->OfferId, NewOffer);
	}
}

void FOnlineStoreGooglePlayV2::GetOffers(TArray<FOnlineStoreOfferRef>& OutOffers) const
{
	for (const auto& CachedEntry : CachedOffers)
	{
		const TSharedRef<FOnlineStoreOffer>& CachedOffer = CachedEntry.Value;
		OutOffers.Add(CachedOffer);
	}
}

TSharedPtr<FOnlineStoreOffer> FOnlineStoreGooglePlayV2::GetOffer(const FUniqueOfferId& OfferId) const
{
	TSharedPtr<FOnlineStoreOffer> Result;

	const TSharedRef<FOnlineStoreOffer>* Existing = CachedOffers.Find(OfferId);
	if (Existing != nullptr)
	{
		Result = (*Existing);
	}

	return Result;
}

#if !OSSGOOGLEPLAY_WITH_AIDL

JNI_METHOD void Java_com_epicgames_ue4_GooglePlayStoreHelper_nativeQueryComplete(JNIEnv* jenv, jobject thiz, jsize responseCode, jobjectArray productIDs, jobjectArray titles, jobjectArray descriptions, jobjectArray prices, jfloatArray pricesRaw, jobjectArray currencyCodes, jobjectArray originalJson)
{
	TArray<FOnlineStoreOffer> ProvidedProductInformation;
	EGooglePlayBillingResponseCode EGPResponse = (EGooglePlayBillingResponseCode)responseCode;
	bool bWasSuccessful = (EGPResponse == EGooglePlayBillingResponseCode::Ok);

	if (jenv && bWasSuccessful)
	{
		jsize NumProducts = jenv->GetArrayLength(productIDs);
		jsize NumTitles = jenv->GetArrayLength(titles);
		jsize NumDescriptions = jenv->GetArrayLength(descriptions);
		jsize NumPrices = jenv->GetArrayLength(prices);
		jsize NumPricesRaw = jenv->GetArrayLength(pricesRaw);
		jsize NumCurrencyCodes = jenv->GetArrayLength(currencyCodes);
		jsize NumJsonStrings = jenv->GetArrayLength(originalJson);

		ensure((NumProducts == NumTitles) && (NumProducts == NumDescriptions) && (NumProducts == NumPrices) && (NumProducts == NumPricesRaw) && (NumProducts == NumCurrencyCodes) && (NumProducts == NumJsonStrings));

		jfloat* PricesRaw = jenv->GetFloatArrayElements(pricesRaw, 0);

		for (jsize Idx = 0; Idx < NumProducts; Idx++)
		{
			// Build the product information strings.

			FOnlineStoreOffer NewProductInfo;

			NewProductInfo.OfferId = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(productIDs, Idx));

			int32 OpenParenIdx = -1;
			int32 CloseParenIdx = -1;
			FString Title = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(titles, Idx));
			if (Title.FindLastChar(TEXT(')'), CloseParenIdx) && Title.FindLastChar(TEXT('('), OpenParenIdx) && (OpenParenIdx < CloseParenIdx))
			{
				Title = Title.Left(OpenParenIdx).TrimEnd();
			}
			NewProductInfo.Title = FText::FromString(Title);

			NewProductInfo.Description = FText::FromString(FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(descriptions, Idx)));
			NewProductInfo.PriceText = FText::FromString(FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(prices, Idx)));
			NewProductInfo.CurrencyCode = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(currencyCodes, Idx));

			// Convert the backend stated price into its base units
			FInternationalization& I18N = FInternationalization::Get();
			const FCulture& Culture = *I18N.GetCurrentCulture();

			const FDecimalNumberFormattingRules& FormattingRules = Culture.GetCurrencyFormattingRules(NewProductInfo.CurrencyCode);
			const FNumberFormattingOptions& FormattingOptions = FormattingRules.CultureDefaultFormattingOptions;
			double Val = static_cast<double>(PricesRaw[Idx]) * static_cast<double>(FMath::Pow(10.0f, FormattingOptions.MaximumFractionalDigits));

			NewProductInfo.NumericPrice = FMath::TruncToInt(Val + 0.5);

			//Loop through original json data and populate dynamic map.
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(originalJson, Idx)));
			if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
			{
				for (TPair<FString, TSharedPtr<FJsonValue>> JsonValue : JsonObject->Values)
				{
					NewProductInfo.DynamicFields.Add(JsonValue.Key, JsonValue.Value->AsString());
				}
			}

			NewProductInfo.ReleaseDate = FDateTime::MinValue();
			NewProductInfo.ExpirationDate = FDateTime::MaxValue();

			ProvidedProductInformation.Add(NewProductInfo);

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\nProduct Identifier: %s, Name: %s, Description: %s, Price: %s, Price Raw: %d, Currency Code: %s\n"),
				*NewProductInfo.OfferId,
				*NewProductInfo.Title.ToString(),
				*NewProductInfo.Description.ToString(),
				*NewProductInfo.GetDisplayPrice().ToString(),
				NewProductInfo.NumericPrice,
				*NewProductInfo.CurrencyCode);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.ProcessQueryIapResult"), STAT_FSimpleDelegateGraphTask_ProcessQueryIapResult, STATGROUP_TaskGraphTasks);

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Adding task Success: %d Response: %s"), bWasSuccessful, ToString(EGPResponse));

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=]()
	{
		if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get(GOOGLEPLAY_SUBSYSTEM))
		{
			FOnlineSubsystemGooglePlay* const OnlineSubGP = static_cast<FOnlineSubsystemGooglePlay* const>(OnlineSub);
			if (OnlineSubGP)
			{
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("TriggerOnGooglePlayAvailableIAPQueryCompleteDelegates %s Size: %d"), ToString(EGPResponse), ProvidedProductInformation.Num());
				OnlineSubGP->TriggerOnGooglePlayAvailableIAPQueryCompleteDelegates(EGPResponse, ProvidedProductInformation);
			}
		}
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("In-App Purchase query was completed  %s\n"), bWasSuccessful ? TEXT("successfully") : TEXT("unsuccessfully"));
	}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_ProcessQueryIapResult),
		nullptr,
		ENamedThreads::GameThread
		);
}

#endif