// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/** 
 * Filter for blacklisting or whitelisting items with ownership to allow change cleanup
 */
class FNamedBlacklist
{
public:

	/** Returns true if passes filter restrictions */
	bool PassesFilter(const FName ItemName) const
	{
		if (BlacklistAll.Num() > 0)
		{
			return false;
		}
		else if (Blacklist.Contains(ItemName))
		{
			return false;
		}
		else if (Whitelist.Num() > 0 && !Whitelist.Contains(ItemName))
		{
			return false;
		}

		return true;
	}
	
	/** Add item to blacklist, this specific item will be filtered out */
	void AddBlacklistItem(const FName OwnerName, const FName ItemName)
	{
		Blacklist.FindOrAdd(ItemName).AddUnique(OwnerName);
	}

	/** Add item to whitelist after which all items not in the whitelist will be filtered out */
	void AddWhitelistItem(const FName OwnerName, const FName ItemName)
	{
		Whitelist.FindOrAdd(ItemName).AddUnique(OwnerName);
	}

	/** Set to filter out all items */
	void AddBlacklistAll(const FName OwnerName)
	{
		BlacklistAll.AddUnique(OwnerName);
	}

	/** Removes all filtering changes associated with a specific owner name */
	void UnregisterOwner(const FName OwnerName)
	{
		for (auto It = Blacklist.CreateIterator(); It; ++It)
		{
			It->Value.Remove(OwnerName);
			if (It->Value.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = Whitelist.CreateIterator(); It; ++It)
		{
			It->Value.Remove(OwnerName);
			if (It->Value.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}

		BlacklistAll.Remove(OwnerName);
	}

	/** True if has filters active */
	bool HasFiltering() const
	{
		return Blacklist.Num() > 0 || Whitelist.Num() > 0 || BlacklistAll.Num() > 0;
	}

	/** Combine two filters together */
	void Append(const FNamedBlacklist& Other)
	{
		for (const auto& It : Other.Blacklist)
		{
			for (const auto& OwnerName : It.Value)
			{
				AddBlacklistItem(OwnerName, It.Key);
			}
		}

		for (const auto& It : Other.Whitelist)
		{
			for (const auto& OwnerName : It.Value)
			{
				AddWhitelistItem(OwnerName, It.Key);
			}
		}

		for (const auto& OwnerName : Other.BlacklistAll)
		{
			AddBlacklistAll(OwnerName);
		}
	}

private:

	/** List of owner names that requested a specific item filtered, allows unregistering specific set of changes by a given plugin or system */
	typedef TArray<FName> FNamedBlacklistOwners;

	/** List if items to filter out */
	TMap<FName, FNamedBlacklistOwners> Blacklist;

	/** List of items to allow, if not empty all items will be filtered out unless they are in the list */
	TMap<FName, FNamedBlacklistOwners> Whitelist;

	/** List of owner names that requested all items to be filtered out */
	FNamedBlacklistOwners BlacklistAll;
};

/**
 * Collection of blacklisting filters
 */
class FNamedBlacklistCollection
{
public:

	/** Returns true if passes filter restrictions */
	bool PassesFilter(const FName GroupName, const FName ItemName) const
	{
		const FNamedBlacklist* Filter = Filters.Find(GroupName);
		return !Filter || Filter->PassesFilter(ItemName);
	}

	/** Add item to blacklist */
	void AddBlacklistItem(const FName OwnerName, const FName GroupName, const FName ItemName)
	{
		Filters.FindOrAdd(GroupName).AddBlacklistItem(OwnerName, ItemName);
	}

	/** Add item to whitelist */
	void AddWhitelistItem(const FName OwnerName, const FName GroupName, const FName ItemName)
	{
		Filters.FindOrAdd(GroupName).AddWhitelistItem(OwnerName, ItemName);
	}

	/** Sets group to fully blacklisted */
	void AddBlacklistAll(const FName OwnerName, const FName GroupName)
	{
		Filters.FindOrAdd(GroupName).AddBlacklistAll(OwnerName);
	}

	/** Remove filtering associated with owner name */
	void UnregisterOwner(const FName OwnerName)
	{
		for (auto GroupIt = Filters.CreateIterator(); GroupIt; ++GroupIt)
		{
			FNamedBlacklist& Filter = GroupIt->Value;
			Filter.UnregisterOwner(OwnerName);
			if (!Filter.HasFiltering())
			{
				GroupIt.RemoveCurrent();
			}
		}
	}

private:

	/** List of filters */
	TMap<FName, FNamedBlacklist> Filters;
};
