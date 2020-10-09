// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineStoreInterfaceV2.h"
#include "OnlineSubsystemEOSPackage.h"
#include "OnlineSubsystemEOSTypes.h"

#if WITH_EOS_SDK
	#include "eos_ecom_types.h"

class UWorld;

/**
 * Implementation for online store via EGS
 */
class FOnlineStoreEOS :
	public IOnlineStoreV2,
	public TSharedFromThis<FOnlineStoreEOS, ESPMode::ThreadSafe>
{
public:
	virtual ~FOnlineStoreEOS() = default;

// Begin IOnlineStoreV2
	virtual void QueryCategories(const FUniqueNetId& UserId, const FOnQueryOnlineStoreCategoriesComplete& Delegate) override;
	virtual void GetCategories(TArray<FOnlineStoreCategory>& OutCategories) const override;
	virtual void QueryOffersByFilter(const FUniqueNetId& UserId, const FOnlineStoreFilter& Filter, const FOnQueryOnlineStoreOffersComplete& Delegate) override;
	virtual void QueryOffersById(const FUniqueNetId& UserId, const TArray<FUniqueOfferId>& OfferIds, const FOnQueryOnlineStoreOffersComplete& Delegate) override;
	virtual void GetOffers(TArray<FOnlineStoreOfferRef>& OutOffers) const override;
	virtual TSharedPtr<FOnlineStoreOffer> GetOffer(const FUniqueOfferId& OfferId) const override;
// End IOnlineStoreV2

PACKAGE_SCOPE:
	FOnlineStoreEOS(FOnlineSubsystemEOS* InSubsystem);

	bool HandleOffersExec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar);

private:
	/** Default constructor disabled */
	FOnlineStoreEOS() = delete;

	void QueryOffers(const FUniqueNetId& UserId, const FOnQueryOnlineStoreOffersComplete& Delegate);

	/** Reference to the main EOS subsystem */
	FOnlineSubsystemEOS* EOSSubsystem;

	/** The set of offers for this title */
	TArray<FOnlineStoreOfferRef> CachedOffers;
	/** List of offer ids for this title */
	TArray<FUniqueOfferId> CachedOfferIds;
};

typedef TSharedPtr<FOnlineStoreEOS, ESPMode::ThreadSafe> FOnlineStoreEOSPtr;

#endif
