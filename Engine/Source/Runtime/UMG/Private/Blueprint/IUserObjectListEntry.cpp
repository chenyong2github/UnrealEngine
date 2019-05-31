// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprint/IUserObjectListEntry.h"
#include "Blueprint/UserWidget.h"
#include "Components/ListView.h"

UUserObjectListEntry::UUserObjectListEntry(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
}

void IUserObjectListEntry::NativeOnListItemObjectSet(UObject* ListItemObject)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Making sure the deprecated path still functions, remove this when the old method is deleted
	SetListItemObjectInternal(ListItemObject);	
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	Execute_OnListItemObjectSet(Cast<UObject>(this), ListItemObject);
}

UObject* IUserObjectListEntry::GetListItemObjectInternal() const
{
	return UUserObjectListEntryLibrary::GetListItemObject(Cast<UUserWidget>(const_cast<IUserObjectListEntry*>(this)));
}

void IUserObjectListEntry::SetListItemObject(UUserWidget& ListEntryWidget, UObject* ListItemObject)
{
	if (IUserObjectListEntry* NativeImplementation = Cast<IUserObjectListEntry>(&ListEntryWidget))
	{
		NativeImplementation->NativeOnListItemObjectSet(ListItemObject);
	}
	else if (ListEntryWidget.Implements<UUserObjectListEntry>())
	{
		Execute_OnListItemObjectSet(&ListEntryWidget, ListItemObject);
	}
}

UObject* UUserObjectListEntryLibrary::GetListItemObject(TScriptInterface<IUserObjectListEntry> UserObjectListEntry)
{
	if (UUserWidget* EntryWidget = Cast<UUserWidget>(UserObjectListEntry.GetObject()))
	{
		const UListView* OwningListView = Cast<UListView>(UUserListEntryLibrary::GetOwningListView(EntryWidget));
		if (UObject* const* ListItem = OwningListView ? OwningListView->ItemFromEntryWidget(*EntryWidget) : nullptr)
		{
			return *ListItem;
		}
	}
	return nullptr;
}
