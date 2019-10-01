// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapContactsTypes.h"
#include "MagicLeapContactsComponent.generated.h"

/**
	Component that provides access to the Contacts API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPCONTACTS_API UMagicLeapContactsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Starts up the Contacts API. */
	void BeginPlay() override;

	/** Shuts down the Contacts API */
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
		Initiates the creation of a new contact.
		@param Contact The contact to be created.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid AddContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the update of an existing contact.
		@param Contact The contact to be updated.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid EditContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the deletion of an existing contact.
		@param Contact The contact to be deleted.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid DeleteContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the retrieval of the entire contacts list from the cloud.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid RequestContactsAsync();

	/**
		Initiates a search for contacts with a given query across specified fields.
		@param Query The search string to look for instances of.
		@param SearchField The field within the contact to match the query against.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField);

	/**
		Cancels a request corresponding to the FGuid.
		@param RequestHandle The unique identifier of the request (returned by all contact request functions).
		@return True if the cancellation succeeded, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	bool CancelRequest(const FGuid& RequestHandle);

private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FSingleContactResultDelegateMulti OnAddContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FSingleContactResultDelegateMulti OnEditContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FSingleContactResultDelegateMulti OnDeleteContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMultipleContactsResultDelegateMulti OnRequestContactsResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMultipleContactsResultDelegateMulti OnSearchContactsResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FContactsLogMessageMulti OnLogMessage;
};
