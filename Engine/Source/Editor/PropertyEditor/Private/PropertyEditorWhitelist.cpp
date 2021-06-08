// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorWhitelist.h"

void FPropertyEditorWhitelist::AddWhitelist(TSoftObjectPtr<UStruct> Struct, const FBlacklistNames& Whitelist)
{
	FBlacklistNames& Blacklist = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Blacklist.Append(Whitelist);
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
	FBlacklistNames& Blacklist = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Blacklist.AddWhitelistItem(Owner, PropertyName);
	CachedPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::RemoveFromWhitelist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FBlacklistNames& Blacklist = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	if (Blacklist.RemoveWhitelistItem(Owner, PropertyName))
	{
		CachedPropertyEditorWhitelist.Reset();
		WhitelistUpdatedDelegate.Broadcast();
	}
}

void FPropertyEditorWhitelist::AddToBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FBlacklistNames& Blacklist = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	Blacklist.AddBlacklistItem(Owner, PropertyName);
	CachedPropertyEditorWhitelist.Reset();
	WhitelistUpdatedDelegate.Broadcast();
}

void FPropertyEditorWhitelist::RemoveFromBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FBlacklistNames& Blacklist = RawPropertyEditorWhitelist.FindOrAdd(Struct);
	if (Blacklist.RemoveBlacklistItem(Owner, PropertyName))
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
			NewWhitelist.Append(GetCachedWhitelistForStruct(SuperStruct));
		}

		// Append this struct's whitelist on top of the parent's whitelist
		const FBlacklistNames* StructWhitelist = RawPropertyEditorWhitelist.Find(Struct);
		if (StructWhitelist)
		{
			NewWhitelist.Append(*StructWhitelist);
		}
		else if (NewWhitelist.GetWhitelist().Num() > 0)
		{
			// TODO: Add some option to allow "if Struct is user-generated, whitelist all properties from that Struct"
		}

		return CachedPropertyEditorWhitelist.Add(Struct, NewWhitelist);
	}
}

bool FPropertyEditorWhitelist::IsSpecificPropertyWhitelisted(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FBlacklistNames* StructWhitelist = RawPropertyEditorWhitelist.Find(ObjectStruct);
	if (StructWhitelist)
	{
		return StructWhitelist->GetWhitelist().Contains(PropertyName);
	}
	return false;
}

bool FPropertyEditorWhitelist::IsSpecificPropertyBlacklisted(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FBlacklistNames* StructWhitelist = RawPropertyEditorWhitelist.Find(ObjectStruct);
	if (StructWhitelist)
	{
		return StructWhitelist->GetBlacklist().Contains(PropertyName);
	}
	return false;
}
