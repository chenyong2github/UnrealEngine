// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapContactsComponent.h"
#include "MagicLeapContactsPlugin.h"

void UMagicLeapContactsComponent::BeginPlay()
{
	Super::BeginPlay();
	GET_MAGIC_LEAP_CONTACTS_PLUGIN()->Startup();
	GET_MAGIC_LEAP_CONTACTS_PLUGIN()->SetLogDelegate(OnLogMessage);
}

void UMagicLeapContactsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GET_MAGIC_LEAP_CONTACTS_PLUGIN()->Shutdown();
	Super::EndPlay(EndPlayReason);
}

FGuid UMagicLeapContactsComponent::AddContactAsync(const FMagicLeapContact& Contact)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->AddContactAsync(Contact, OnAddContactResult);
}

FGuid UMagicLeapContactsComponent::EditContactAsync(const FMagicLeapContact& Contact)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->EditContactAsync(Contact, OnEditContactResult);
}

FGuid UMagicLeapContactsComponent::DeleteContactAsync(const FMagicLeapContact& Contact)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->DeleteContactAsync(Contact, OnDeleteContactResult);
}

FGuid UMagicLeapContactsComponent::RequestContactsAsync()
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->RequestContactsAsync(OnRequestContactsResult);
}

FGuid UMagicLeapContactsComponent::SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->SearchContactsAsync(Query, SearchField, OnSearchContactsResult);
}

bool UMagicLeapContactsComponent::CancelRequest(const FGuid& RequestHandle)
{
	return GET_MAGIC_LEAP_CONTACTS_PLUGIN()->CancelRequest(RequestHandle);
}
