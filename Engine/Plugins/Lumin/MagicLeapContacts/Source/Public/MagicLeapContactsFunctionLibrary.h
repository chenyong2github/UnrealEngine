// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapContactsTypes.h"
#include "MagicLeapContactsFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPCONTACTS_API UMagicLeapContactsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Initialize all necessary resources for using the Contacts API. */
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static bool Startup();

	/** Deinitialize all resources for this API. */
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static bool Shutdown();

	/**
		Initiates the creation of a new contact.
		@param Contact The contact to be created.
		@param ResultDelegate The delegate to be notified upon creation of the contact.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid AddContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& ResultDelegate);

	/**
		Initiates the update of an existing contact.
		@param Contact The contact to be updated.
		@param ResultDelegate The delegate to be notified upon update of the contact.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid EditContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& ResultDelegate);

	/** 
		Initiates the deletion of an existing contact.
		@param Contact The contact to be deleted.
		@param ResultDelegate The delegate to be notified upon deletion of the contact.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid DeleteContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& ResultDelegate);

	/** 
		Initiates the retrieval of the entire contacts list from the cloud.
		@param ResultDelegate The delegate to be notified once the contacts list has been retrieved from the cloud.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid RequestContactsAsync(const FMultipleContactsResultDelegate& ResultDelegate);

	/**
		Initiates a search for contacts with a given query across specified fields.
		@param Query The search string to look for instances of.
		@param SearchField The field within the contact to match the query against.
		@param ResultDelegate The delegate to be notified upon completion of the query.
		@return A unique identifier for this request (required if request needs to be cancelled).
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMultipleContactsResultDelegate& ResultDelegate);

	/**
		Cancels a request corresponding to the FGuid.
		@param RequestHandle The unique identifier of the request (returned by all contact request functions).
		@return True if the cancellation succeeded, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static bool CancelRequest(const FGuid& RequestHandle);

	/**
		Sets the delegate by which the system can pass log messages back to the calling blueprint.
		@param LogDelegate The delegate by which the system will return log messages to the calling blueprint.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static bool SetLogDelegate(const FContactsLogMessage& LogDelegate);
};
