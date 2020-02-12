// Copyright Epic Games, Inc. All Rights Reserved.

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
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid AddContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the update of an existing contact.
		@param Contact The contact to be updated.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid EditContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the deletion of an existing contact.
		@param Contact The contact to be deleted.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid DeleteContactAsync(const FMagicLeapContact& Contact);

	/**
		Initiates the retrieval of the entire contacts list from the cloud.
		@param MaxNumResults The maximum number of results to return.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid RequestContactsAsync(int32 MaxNumResults);

	/**
		Pops up a dialog allowing the user to manually select the contacts they wish to query.
		@param MaxNumResults The maximum number of contacts to display (values greater than number of contacts will result in an invalid param error).
		@param SearchField Specifies which field(s) to retrieve for each selected contact.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid SelectContactsAsync(int32 MaxNumResults, EMagicLeapContactsSearchField SearchField);

	/**
		Initiates a search for contacts with a given query across specified fields.
		@param Query The search string to look for instances of.
		@param SearchField The field within the contact to match the query against.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField);

private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapSingleContactResultDelegateMulti OnAddContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapSingleContactResultDelegateMulti OnEditContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapSingleContactResultDelegateMulti OnDeleteContactResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapMultipleContactsResultDelegateMulti OnRequestContactsResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapMultipleContactsResultDelegateMulti OnSelectContactsResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapMultipleContactsResultDelegateMulti OnSearchContactsResult;

	UPROPERTY(BlueprintAssignable, Category = "Contacts | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapContactsLogMessageMulti OnLogMessage;
};
