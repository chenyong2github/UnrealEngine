// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapContactsFunctionLibrary.h"
#include "MagicLeapContactsPlugin.h"

bool UMagicLeapContactsFunctionLibrary::Startup()
{
	return GetMagicLeapContactsPlugin().Startup();
}

bool UMagicLeapContactsFunctionLibrary::Shutdown()
{
	return GetMagicLeapContactsPlugin().Shutdown();
}

FGuid UMagicLeapContactsFunctionLibrary::AddContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& InResultDelegate)
{
	FMagicLeapSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().AddContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::EditContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& InResultDelegate)
{
	FMagicLeapSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().EditContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::DeleteContactAsync(const FMagicLeapContact& Contact, const FMagicLeapSingleContactResultDelegate& InResultDelegate)
{
	FMagicLeapSingleContactResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().DeleteContactAsync(Contact, ResultDelegate);
}

FGuid UMagicLeapContactsFunctionLibrary::RequestContactsAsync(const FMagicLeapMultipleContactsResultDelegate& InResultDelegate, int32 MaxNumResults)
{
	FMagicLeapMultipleContactsResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().RequestContactsAsync(ResultDelegate, MaxNumResults);
}

FGuid UMagicLeapContactsFunctionLibrary::SelectContactsAsync(const FMagicLeapMultipleContactsResultDelegate& InResultDelegate, int32 MaxNumResults, EMagicLeapContactsSearchField SearchField)
{
	FMagicLeapMultipleContactsResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().SelectContactsAsync(ResultDelegate, MaxNumResults, SearchField);
}

FGuid UMagicLeapContactsFunctionLibrary::SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMagicLeapMultipleContactsResultDelegate& InResultDelegate)
{
	FMagicLeapMultipleContactsResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapContactsPlugin().SearchContactsAsync(Query, SearchField, ResultDelegate);
}

bool UMagicLeapContactsFunctionLibrary::SetLogDelegate(const FMagicLeapContactsLogMessage& InLogDelegate)
{
	FMagicLeapContactsLogMessageMulti LogDelegate;
	LogDelegate.Add(InLogDelegate);
	return GetMagicLeapContactsPlugin().SetLogDelegate(LogDelegate);
}
