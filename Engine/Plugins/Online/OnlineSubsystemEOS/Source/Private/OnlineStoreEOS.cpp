// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStoreEOS.h"

#if WITH_EOS_SDK

#include "OnlineSubsystemEOS.h"
#include "UserManagerEOS.h"
#include "eos_ecom.h"


FOnlineStoreEOS::FOnlineStoreEOS(FOnlineSubsystemEOS* InSubsystem)
	: EOSSubsystem(InSubsystem)
{
	check(EOSSubsystem != nullptr);
}

void FOnlineStoreEOS::QueryCategories(const FUniqueNetId& UserId, const FOnQueryOnlineStoreCategoriesComplete& Delegate)
{
	Delegate.ExecuteIfBound(false, TEXT("QueryCategories Not Implemented"));
}

void FOnlineStoreEOS::GetCategories(TArray<FOnlineStoreCategory>& OutCategories) const
{
	OutCategories.Reset();
}

void FOnlineStoreEOS::QueryOffersByFilter(const FUniqueNetId& UserId, const FOnlineStoreFilter& Filter, const FOnQueryOnlineStoreOffersComplete& Delegate)
{
	QueryOffers(UserId, Delegate);
}

void FOnlineStoreEOS::QueryOffersById(const FUniqueNetId& UserId, const TArray<FUniqueOfferId>& OfferIds, const FOnQueryOnlineStoreOffersComplete& Delegate)
{
	QueryOffers(UserId, Delegate);
}

typedef TEOSCallback<EOS_Ecom_OnQueryOffersCallback, EOS_Ecom_QueryOffersCallbackInfo> FQueryOffersCallback;

void FOnlineStoreEOS::QueryOffers(const FUniqueNetId& UserId, const FOnQueryOnlineStoreOffersComplete& Delegate)
{
	if (CachedOfferIds.Num() && CachedOffers.Num())
	{
		Delegate.ExecuteIfBound(true, CachedOfferIds, TEXT("Returning cached offers"));
		return;
	}
	EOS_EpicAccountId AccountId = EOSSubsystem->UserManager->GetEpicAccountId(UserId);
	if (AccountId == nullptr)
	{
		Delegate.ExecuteIfBound(false, TArray<FUniqueOfferId>(), TEXT("Can't query offers for a null user"));
		return;
	}

	CachedOfferIds.Reset();
	CachedOffers.Reset();

	EOS_Ecom_QueryOffersOptions Options = { };
	Options.ApiVersion = EOS_ECOM_QUERYOFFERS_API_LATEST;
	Options.LocalUserId = AccountId;

	FQueryOffersCallback* CallbackObj = new FQueryOffersCallback();
	CallbackObj->CallbackLambda = [this, OnComplete = FOnQueryOnlineStoreOffersComplete(Delegate)](const EOS_Ecom_QueryOffersCallbackInfo* Data)
	{
		EOS_EResult Result = Data->ResultCode;
		if (Result != EOS_EResult::EOS_Success)
		{
			OnComplete.ExecuteIfBound(false, CachedOfferIds, EOS_EResult_ToString(Data->ResultCode));
			return;
		}

		EOS_Ecom_GetOfferCountOptions CountOptions = { };
		CountOptions.ApiVersion = EOS_ECOM_GETOFFERCOUNT_API_LATEST;
		CountOptions.LocalUserId = Data->LocalUserId;
		uint32 OfferCount = EOS_Ecom_GetOfferCount(EOSSubsystem->EcomHandle, &CountOptions);

		EOS_Ecom_CopyOfferByIndexOptions OfferOptions = { };
		OfferOptions.ApiVersion = EOS_ECOM_COPYOFFERBYINDEX_API_LATEST;
		OfferOptions.LocalUserId = Data->LocalUserId;
		// Iterate and parse the offer list
		for (uint32 OfferIndex = 0; OfferIndex < OfferCount; OfferIndex++)
		{
			EOS_Ecom_CatalogOffer* Offer = nullptr;
			OfferOptions.OfferIndex = OfferIndex;
			EOS_EResult OfferResult = EOS_Ecom_CopyOfferByIndex(EOSSubsystem->EcomHandle, &OfferOptions, &Offer);
			if (OfferResult != EOS_EResult::EOS_Success)
			{
				continue;
			}
			FOnlineStoreOfferRef OfferRef(new FOnlineStoreOffer());
			OfferRef->OfferId = Offer->Id;

			OfferRef->Title = FText::FromString(Offer->TitleText);
			OfferRef->Description = FText::FromString(Offer->DescriptionText);
			OfferRef->LongDescription = FText::FromString(Offer->LongDescriptionText);

			OfferRef->ExpirationDate = FDateTime(Offer->ExpirationTimestamp);

			OfferRef->CurrencyCode = Offer->CurrencyCode;
			if (Offer->PriceResult == EOS_EResult::EOS_Success)
			{
				OfferRef->RegularPrice = Offer->OriginalPrice;
				OfferRef->NumericPrice = Offer->CurrentPrice;
				OfferRef->DiscountType = Offer->DiscountPercentage == 0 ? EOnlineStoreOfferDiscountType::NotOnSale : EOnlineStoreOfferDiscountType::DiscountAmount;
			}

			CachedOffers.Add(OfferRef);
			CachedOfferIds.Add(OfferRef->OfferId);

			EOS_Ecom_CatalogOffer_Release(Offer);
		}

		OnComplete.ExecuteIfBound(true, CachedOfferIds, TEXT(""));
	};
	EOS_Ecom_QueryOffers(EOSSubsystem->EcomHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
}

void FOnlineStoreEOS::GetOffers(TArray<FOnlineStoreOfferRef>& OutOffers) const
{
	OutOffers = CachedOffers;
}

TSharedPtr<FOnlineStoreOffer> FOnlineStoreEOS::GetOffer(const FUniqueOfferId& OfferId) const
{
	for (FOnlineStoreOfferRef Offer : CachedOffers)
	{
		if (Offer->OfferId == OfferId)
		{
			return Offer;
		}
	}
	return nullptr;
}

void FOnlineStoreEOS::Checkout(const FUniqueNetId& UserId, const FPurchaseCheckoutRequest& CheckoutRequest, const FOnPurchaseCheckoutComplete& Delegate)
{

}

void FOnlineStoreEOS::FinalizePurchase(const FUniqueNetId& UserId, const FString& ReceiptId)
{

}

void FOnlineStoreEOS::RedeemCode(const FUniqueNetId& UserId, const FRedeemCodeRequest& RedeemCodeRequest, const FOnPurchaseRedeemCodeComplete& Delegate)
{

}

void FOnlineStoreEOS::QueryReceipts(const FUniqueNetId& UserId, bool bRestoreReceipts, const FOnQueryReceiptsComplete& Delegate)
{
}

void FOnlineStoreEOS::GetReceipts(const FUniqueNetId& UserId, TArray<FPurchaseReceipt>& OutReceipts) const
{
	OutReceipts = CachedReceipts;
}

void FOnlineStoreEOS::FinalizeReceiptValidationInfo(const FUniqueNetId& UserId, FString& InReceiptValidationInfo, const FOnFinalizeReceiptValidationInfoComplete& Delegate)
{

}

bool FOnlineStoreEOS::HandleEcomExec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("OFFERS")))
	{
		QueryOffers(*EOSSubsystem->UserManager->GetLocalUniqueNetIdEOS(),
			FOnQueryOnlineStoreOffersComplete::CreateLambda([this](bool bWasSuccessful, const TArray<FUniqueOfferId>& OfferIds, const FString& ErrorStr)
		{
			UE_LOG_ONLINE(Error, TEXT("QueryOffers: %s with error (%s)"), bWasSuccessful ? TEXT("succeeded") : TEXT("failed"), *ErrorStr);

			for (const FUniqueOfferId& OfferId : OfferIds)
			{
				UE_LOG_ONLINE(Error, TEXT("OfferId: %s"), *OfferId);
			}

		}));
		return true;
	}
	return false;
}

#endif