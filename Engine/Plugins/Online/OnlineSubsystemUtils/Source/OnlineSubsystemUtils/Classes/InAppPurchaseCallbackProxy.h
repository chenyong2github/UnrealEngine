// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "Interfaces/OnlineStoreInterface.h"
#include "InAppPurchaseCallbackProxy.generated.h"

/**
 * Micro-transaction purchase information
 */
USTRUCT(BlueprintType)
struct FInAppPurchaseReceiptInfo
{
	GENERATED_USTRUCT_BODY()

		// The item name
		UPROPERTY(BlueprintReadOnly, Category = ProductInfo)
		FString ItemName;

	// The unique product identifier
	UPROPERTY(BlueprintReadOnly, Category = ProductInfo)
		FString ItemId;

	// the unique transaction identifier
	UPROPERTY(BlueprintReadOnly, Category = ProductInfo)
		FString ValidationInfo;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInAppPurchaseResult, EInAppPurchaseState::Type, PurchaseStatus, const FInAppPurchaseProductInfo&, InAppPurchaseReceipts);

UCLASS(MinimalAPI)
class UE_DEPRECATED(4.26, "UInAppPurchaseCallbackProxy is deprecated, please use UInAppPurchaseCallbackProxy2 instead.") UInAppPurchaseCallbackProxy : public UObject
{
	GENERATED_UCLASS_BODY()

	// Called when there is a successful In-App Purchase transaction
	UPROPERTY(BlueprintAssignable)
	FInAppPurchaseResult OnSuccess;

	// Called when there is an unsuccessful In-App Purchase transaction
	UPROPERTY(BlueprintAssignable)
	FInAppPurchaseResult OnFailure;

	// Kicks off a transaction for the provided product identifier
	UFUNCTION(BlueprintCallable, meta = (DisplayName="Make an In-App Purchase"), Category="Online|InAppPurchase")
	static UInAppPurchaseCallbackProxy* CreateProxyObjectForInAppPurchase(class APlayerController* PlayerController, const FInAppPurchaseProductRequest& ProductRequest);

public:

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

private:

	/** Called by the InAppPurchase system when the transaction has finished */
	void OnInAppPurchaseComplete_Delayed();
	void OnInAppPurchaseComplete(EInAppPurchaseState::Type CompletionState);

	/** Unregisters our delegate from the In-App Purchase system */
	void RemoveDelegate();

	/** Triggers the In-App Purchase Transaction for the specifed user; the Purchase Request object must already be set up */
	void Trigger(class APlayerController* PlayerController, const FInAppPurchaseProductRequest& ProductRequest);

private:

	/** Delegate called when a InAppPurchase has been successfully read */
	FOnInAppPurchaseCompleteDelegate InAppPurchaseCompleteDelegate;

	/** Handle to the registered InAppPurchaseCompleteDelegate */
	FDelegateHandle InAppPurchaseCompleteDelegateHandle;

	/** The InAppPurchase read request */
	FOnlineInAppPurchaseTransactionPtr PurchaseRequest;

	/** Did we fail immediately? */
	bool bFailedToEvenSubmit;

	/** Pointer to the world, needed to delay the results slightly */
	TWeakObjectPtr<UWorld> WorldPtr;

	/** Did the purchase succeed? */
	EInAppPurchaseState::Type SavedPurchaseState;
};
