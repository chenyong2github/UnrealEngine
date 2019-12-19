// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"

/** List of owner names that requested a specific item filtered, allows unregistering specific set of changes by a given plugin or system */
typedef TArray<FName> FBlacklistOwners;

class CORE_API FBlacklistNames : public TSharedFromThis<FBlacklistNames>
{
public:
	FBlacklistNames();
	~FBlacklistNames() {}

	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FName Item) const;

	/** Add item to blacklist, this specific item will be filtered out */
	void AddBlacklistItem(const FName OwnerName, const FName Item);

	/** Add item to whitelist after which all items not in the whitelist will be filtered out */
	void AddWhitelistItem(const FName OwnerName, const FName Item);

	/** Set to filter out all items */
	void AddBlacklistAll(const FName OwnerName);
	
	/** True if has filters active */
	bool HasFiltering() const;

	/** Removes all filtering changes associated with a specific owner name */
	void UnregisterOwner(const FName OwnerName);

	/** Combine two filters together */
	void Append(const FBlacklistNames& Other);

	/** Get raw blacklist */
	const TMap<FName, FBlacklistOwners>& GetBlacklist() const { return Blacklist; }
	
	/** Get raw whitelist */
	const TMap<FName, FBlacklistOwners>& GetWhitelist() const { return Whitelist; }

	/** Are all items set to be filtered out */
	bool IsBlacklistAll() const { return Blacklist.Num() > 0; }

	/** Triggered when filter changes */
	FSimpleMulticastDelegate& OnFilterChanged() { return OnFilterChangedDelegate; }

protected:

	/** List if items to filter out */
	TMap<FName, FBlacklistOwners> Blacklist;

	/** List of items to allow, if not empty all items will be filtered out unless they are in the list */
	TMap<FName, FBlacklistOwners> Whitelist;

	/** List of owner names that requested all items to be filtered out */
	FBlacklistOwners BlacklistAll;

	/** Triggered when filter changes */
	FSimpleMulticastDelegate OnFilterChangedDelegate;

	/** Temporarily prevent delegate from being triggered */
	bool bSuppressOnFilterChanged;
};

class CORE_API FBlacklistPaths : public TSharedFromThis<FBlacklistPaths>
{
public:
	FBlacklistPaths();
	~FBlacklistPaths() {}
	
	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FString& Item) const;

	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FName Item) const;

	/** Returns true if passes filter restrictions for path */
	bool PassesStartsWithFilter(const FString& Item) const;

	/** Returns true if passes filter restrictions for path */
	bool PassesStartsWithFilter(const FName Item) const;

	/** Add item to blacklist, this specific item will be filtered out */
	void AddBlacklistItem(const FName OwnerName, const FString& Item);

	/** Add item to whitelist after which all items not in the whitelist will be filtered out */
	void AddWhitelistItem(const FName OwnerName, const FString& Item);

	/** Set to filter out all items */
	void AddBlacklistAll(const FName OwnerName);
	
	/** True if has filters active */
	bool HasFiltering() const;

	/** Removes all filtering changes associated with a specific owner name */
	void UnregisterOwner(const FName OwnerName);

	/** Combine two filters together */
	void Append(const FBlacklistPaths& Other);

	/** Get raw blacklist */
	const TMap<FString, FBlacklistOwners>& GetBlacklist() const { return Blacklist; }
	
	/** Get raw whitelist */
	const TMap<FString, FBlacklistOwners>& GetWhitelist() const { return Whitelist; }

	/** Are all items set to be filtered out */
	bool IsBlacklistAll() const { return Blacklist.Num() > 0; }
	
	/** Triggered when filter changes */
	FSimpleMulticastDelegate& OnFilterChanged() { return OnFilterChangedDelegate; }

protected:

	/** List if items to filter out */
	TMap<FString, FBlacklistOwners> Blacklist;

	/** List of items to allow, if not empty all items will be filtered out unless they are in the list */
	TMap<FString, FBlacklistOwners> Whitelist;

	/** List of owner names that requested all items to be filtered out */
	FBlacklistOwners BlacklistAll;
	
	/** Triggered when filter changes */
	FSimpleMulticastDelegate OnFilterChangedDelegate;

	/** Temporarily prevent delegate from being triggered */
	bool bSuppressOnFilterChanged;
};
