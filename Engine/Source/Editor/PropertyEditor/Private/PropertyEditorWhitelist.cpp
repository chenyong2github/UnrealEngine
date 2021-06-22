// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorWhitelist.h"

#include "UObject/UnrealType.h"

namespace
{
	const FName PropertyEditorWhitelistOwner = "PropertyEditorWhitelist";
}

void FPropertyEditorWhitelist::AddWhitelist(TSoftObjectPtr<UStruct> Struct, const FBlacklistNames& Whitelist, EPropertyEditorWhitelistRules Rules)
{
	FPropertyEditorWhitelistEntry& Entry = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Entry.Whitelist.Append(Whitelist);
	Entry.Rules = Rules;
	// The cache isn't too expensive to recompute, so it is cleared
	// and lazily repopulated any time the raw whitelist changes.
	CachedPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::RemoveWhitelist(TSoftObjectPtr<UStruct> Struct)
{
	if (RawPropertyEditorWhitelist.Remove(Struct) > 0)
	{
		CachedPropertyEditorWhitelist.Reset();
		WhitelistUpdatedDelegate.Broadcast();
	}
}

void FPropertyEditorWhitelist::ClearWhitelist()
{
	TArray<TSoftObjectPtr<UStruct>> Keys;
	RawPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::AddToWhitelist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyEditorWhitelistEntry& Entry = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Entry.Whitelist.AddWhitelistItem(Owner, PropertyName);
	CachedPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::RemoveFromWhitelist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyEditorWhitelistEntry& Entry = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	if (Entry.Whitelist.RemoveWhitelistItem(Owner, PropertyName))
	{
		CachedPropertyEditorWhitelist.Reset();
		WhitelistUpdatedDelegate.Broadcast();
	}
}

void FPropertyEditorWhitelist::AddToBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyEditorWhitelistEntry& Entry = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Entry.Whitelist.AddBlacklistItem(Owner, PropertyName);
	CachedPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::RemoveFromBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyEditorWhitelistEntry& Entry = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	if (Entry.Whitelist.RemoveBlacklistItem(Owner, PropertyName))
	{
		CachedPropertyEditorWhitelist.Reset();
		WhitelistUpdatedDelegate.Broadcast();
	}
}

void FPropertyEditorWhitelist::SetEnabled(bool bEnable)
{
	bEnablePropertyEditorWhitelist = bEnable;
	WhitelistEnabledDelegate.Broadcast();
}

bool FPropertyEditorWhitelist::DoesPropertyPassFilter(const UStruct* ObjectStruct, FName PropertyName) const
{
	if (bEnablePropertyEditorWhitelist && ObjectStruct)
	{
		return GetCachedWhitelistForStruct(ObjectStruct).PassesFilter(PropertyName);
	}
	return true;
}

const FBlacklistNames& FPropertyEditorWhitelist::GetCachedWhitelistForStruct(const UStruct* Struct) const
{
	// Default value doesn't matter since it's a no-op until the first whitelist is encountered, at which
	// point the rules will re-assign the value.
	bool bShouldWhitelistAllProperties = true;
	return GetCachedWhitelistForStructHelper(Struct, bShouldWhitelistAllProperties);
}

const FBlacklistNames& FPropertyEditorWhitelist::GetCachedWhitelistForStructHelper(const UStruct* Struct, bool& bInOutShouldWhitelistAllProperties) const
{
	check(Struct);

	const FBlacklistNames* CachedWhitelist = CachedPropertyEditorWhitelist.Find(Struct);
	if (CachedWhitelist)
	{
		return *CachedWhitelist;
	}
	else
	{
		FBlacklistNames NewWhitelist;

		UStruct* SuperStruct = Struct->GetSuperStruct();
		// Recursively fill the cache for all parent structs
		if (SuperStruct)
		{
			NewWhitelist.Append(GetCachedWhitelistForStructHelper(SuperStruct, bInOutShouldWhitelistAllProperties));
		}

		// Append this struct's whitelist on top of the parent's whitelist
		const FPropertyEditorWhitelistEntry* Entry = RawPropertyEditorWhitelist.Find(Struct);
		bool bIsThisWhitelistEmpty = true;
		if (Entry)
		{
			NewWhitelist.Append(Entry->Whitelist);
			bIsThisWhitelistEmpty = Entry->Whitelist.GetWhitelist().Num() == 0;

			if (Entry->Rules == EPropertyEditorWhitelistRules::WhitelistAllProperties)
			{
				bInOutShouldWhitelistAllProperties = true;
			}
		}

		// Whitelist all properties if the flag is set, the parent Struct has a whitelist, and this Struct has no whitelist
		// If the parent Struct's whitelist is empty then that already implies all properties are visible
		// If this Struct has a whitelist, the manually-specified list always overrides the ShouldWhitelistAllProperties rule
		if (bInOutShouldWhitelistAllProperties && NewWhitelist.GetWhitelist().Num() > 0 && bIsThisWhitelistEmpty)
		{
			for (TFieldIterator<FProperty> Property(Struct, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); Property; ++Property)
			{
				NewWhitelist.AddWhitelistItem(PropertyEditorWhitelistOwner, Property->GetFName());
			}
		}

		// If this Struct has no whitelist, then the ShouldWhitelistAllProperties rule just forwards its current value on to the next subclass.
		// This causes an issue in the case where a Struct should have no whitelisted properties but wants to whitelist all subclass properties.
		// In this case, simply add a dummy entry to the Struct's whitelist that (likely) won't ever collide with a real property name
		if (!bIsThisWhitelistEmpty)
		{
			bInOutShouldWhitelistAllProperties = Entry->Rules == EPropertyEditorWhitelistRules::WhitelistAllSubclassProperties;
		}

		return CachedPropertyEditorWhitelist.Add(Struct, NewWhitelist);
	}
}

bool FPropertyEditorWhitelist::IsSpecificPropertyWhitelisted(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FPropertyEditorWhitelistEntry* Entry = RawPropertyEditorWhitelist.Find(ObjectStruct);
	if (Entry)
	{
		return Entry->Whitelist.GetWhitelist().Contains(PropertyName);
	}
	return false;
}

bool FPropertyEditorWhitelist::IsSpecificPropertyBlacklisted(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FPropertyEditorWhitelistEntry* Entry = RawPropertyEditorWhitelist.Find(ObjectStruct);
	if (Entry)
	{
		return Entry->Whitelist.GetBlacklist().Contains(PropertyName);
	}
	return false;
}
