// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapContactsComponent.h"
#include "MagicLeapContactsPlugin.h"

void UMagicLeapContactsComponent::BeginPlay()
{
	Super::BeginPlay();
	GetMagicLeapContactsPlugin().Startup();
	GetMagicLeapContactsPlugin().SetLogDelegate(OnLogMessage);
}

void UMagicLeapContactsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetMagicLeapContactsPlugin().Shutdown();
	Super::EndPlay(EndPlayReason);
}

FGuid UMagicLeapContactsComponent::AddContactAsync(const FMagicLeapContact& Contact)
{
	return GetMagicLeapContactsPlugin().AddContactAsync(Contact, OnAddContactResult);
}

FGuid UMagicLeapContactsComponent::EditContactAsync(const FMagicLeapContact& Contact)
{
	return GetMagicLeapContactsPlugin().EditContactAsync(Contact, OnEditContactResult);
}

FGuid UMagicLeapContactsComponent::DeleteContactAsync(const FMagicLeapContact& Contact)
{
	return GetMagicLeapContactsPlugin().DeleteContactAsync(Contact, OnDeleteContactResult);
}

FGuid UMagicLeapContactsComponent::RequestContactsAsync(int32 MaxNumResults)
{
	return GetMagicLeapContactsPlugin().RequestContactsAsync(OnRequestContactsResult, MaxNumResults);
}

FGuid UMagicLeapContactsComponent::SelectContactsAsync(int32 MaxNumResults, EMagicLeapContactsSearchField SearchField)
{
	return GetMagicLeapContactsPlugin().SelectContactsAsync(OnSelectContactsResult, MaxNumResults, SearchField);
}

FGuid UMagicLeapContactsComponent::SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField)
{
	return GetMagicLeapContactsPlugin().SearchContactsAsync(Query, SearchField, OnSearchContactsResult);
}
