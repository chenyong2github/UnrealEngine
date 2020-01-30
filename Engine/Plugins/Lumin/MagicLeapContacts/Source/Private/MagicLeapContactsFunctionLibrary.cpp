// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapContactsFunctionLibrary.h"
#include "MagicLeapContactsPlugin.h"

bool UMagicLeapContactsFunctionLibrary::Startup()
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->Startup();
}

bool UMagicLeapContactsFunctionLibrary::Shutdown()
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->Shutdown();
}

FGuid UMagicLeapContactsFunctionLibrary::AddContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& InResultDelegate)
{
	FSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->AddContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::EditContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& InResultDelegate)
{
	FSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->EditContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::DeleteContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegate& InResultDelegate)
{
	FSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->DeleteContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::RequestContactsAsync(const FMultipleContactsResultDelegate& InResultDelegate)
{
	FMultipleContactsResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->RequestContactsAsync(ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMultipleContactsResultDelegate& InResultDelegate)
{
	FMultipleContactsResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->SearchContactsAsync(Query, SearchField, ResultDelegate);
}

bool UMagicLeapContactsFunctionLibrary::CancelRequest(const FGuid& RequestHandle)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->CancelRequest(RequestHandle);
}

bool UMagicLeapContactsFunctionLibrary::SetLogDelegate(const FContactsLogMessage& InLogDelegate)
{
	FContactsLogMessageMulti LogDelegate;
	LogDelegate.Add(InLogDelegate);
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->SetLogDelegate(LogDelegate);
}
