// Copyright Epic Games, Inc. All Rights Reserved.
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
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid AddContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& ResultDelegate);

	/**
		Initiates the update of an existing contact.
		@param Contact The contact to be updated.
		@param ResultDelegate The delegate to be notified upon update of the contact.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid EditContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& ResultDelegate);

	/** 
		Initiates the deletion of an existing contact.
		@param Contact The contact to be deleted.
		@param ResultDelegate The delegate to be notified upon deletion of the contact.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid DeleteContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& ResultDelegate);

	/** 
		Initiates the retrieval of the entire contacts list from the cloud.
		@param MaxNumResults The maximum number of results to return.
		@param ResultDelegate The delegate to be notified once the contacts list has been retrieved from the cloud.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid RequestContactsAsync(const FMagicLeapMultipleContactsResultDelegate& ResultDelegate, int32 MaxNumResults);


	/**
		Pops up a dialog allowing the user to manually select the contacts they wish to query.
		@param MaxNumResults The maximum number of contacts to display (values greater than number of contacts will result in an invalid param error).
		@param SearchField Specifies which field(s) to retrieve for each selected contact.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts | MagicLeap")
	static FGuid SelectContactsAsync(const FMagicLeapMultipleContactsResultDelegate& ResultDelegate, int32 MaxNumResults, EMagicLeapContactsSearchField SearchField);

	/**
		Initiates a search for contacts with a given query across specified fields.
		@param Query The search string to look for instances of.
		@param SearchField The field within the contact to match the query against.
		@param ResultDelegate The delegate to be notified upon completion of the query.
		@return A unique identifier for this request.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMagicLeapMultipleContactsResultDelegate& ResultDelegate);

	/**
		Sets the delegate by which the system can pass log messages back to the calling blueprint.
		@param LogDelegate The delegate by which the system will return log messages to the calling blueprint.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Contacts Function Library | MagicLeap")
	static bool SetLogDelegate(const FMagicLeapContactsLogMessage& LogDelegate);
};
