// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/BlacklistNames.h"

DECLARE_MULTICAST_DELEGATE(FWhitelistUpdated);

class PROPERTYEDITOR_API FPropertyEditorWhitelist
{
public:
	static FPropertyEditorWhitelist& Get()
	{
		static FPropertyEditorWhitelist Whitelist;
		return Whitelist;
	}

	/** Add a set of rules for a specific base UStruct to determine which properties are visible in all details panels */
	void AddWhitelist(TSoftObjectPtr<UStruct> Struct, const FBlacklistNames& Whitelist);
	/** Remove a set of rules for a specific base UStruct to determine which properties are visible in all details panels */
	void RemoveWhitelist(TSoftObjectPtr<UStruct> Struct);
	/** Remove all rules */
	void ClearWhitelist();

	/** Add a specific property to a UStruct's whitelist */
	void AddToWhitelist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName);
	/** Remove a specific property from a UStruct's whitelist */
	void RemoveFromWhitelist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName);
	/** Add a specific property to a UStruct's blacklist */
	void AddToBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName);
	/** Remove a specific property from a UStruct's blacklist */
    void RemoveFromBlacklist(TSoftObjectPtr<UStruct> Struct, const FName PropertyName);

	/** When the whitelist or blacklist for any struct was added to or removed from. */
    FWhitelistUpdated WhitelistUpdatedDelegate;

	/** Controls whether DoesPropertyPassFilter always returns true or performs property-based filtering. */
	bool IsEnabled() const { return bEnablePropertyEditorWhitelist; }
	/** Turn on or off the property editor whitelist. DoesPropertyPassFilter will always return true if disabled. */
	void SetEnabled(bool bEnable) { bEnablePropertyEditorWhitelist = bEnable; }

	/** Whether the Details View should show special menu entries to add/remove items in the whitelist */
	bool ShouldShowMenuEntries() const { return bShouldShowMenuEntries;}
	/** Turn on or off menu entries to modify the whitelist from a Details View */
	void SetShouldShowMenuEntries(bool bShow) { bShouldShowMenuEntries = bShow; }

	/**
	 * Checks if a property passes the whitelist/blacklist filtering specified by PropertyEditorWhitelists
	 * This should be relatively fast as it maintains a flattened cache of all inherited whitelists for every UStruct (which is generated lazily).
	 */
	bool DoesPropertyPassFilter(const UStruct* ObjectStruct, FName PropertyName) const;

	/** Check whether a property exists on the whitelist for a specific Struct - this will return false if the property is whitelisted on a parent Struct */
	bool IsSpecificPropertyWhitelisted(const UStruct* ObjectStruct, FName PropertyName) const;
	/** Check whether a property exists on the blacklist for a specific Struct - this will return false if the property is blacklisted on a parent Struct */
	bool IsSpecificPropertyBlacklisted(const UStruct* ObjectStruct, FName PropertyName) const;

	/** Gets a read-only copy of the original, un-flattened whitelist. */
	const TMap<TSoftObjectPtr<UStruct>, FBlacklistNames>& GetRawWhitelist() const { return RawPropertyEditorWhitelist; }

private:
	/** Whether DoesPropertyPassFilter should perform its whitelist check or always return true */
	bool bEnablePropertyEditorWhitelist = false;
	/** Whether SDetailSingleItemRow should add menu items to add/remove properties to/from the whitelist */
	bool bShouldShowMenuEntries = false;
	
	/** Stores assigned whitelists from AddWhitelist(), which are later flattened and stored in CachedPropertyEditorWhitelist. */
	TMap<TSoftObjectPtr<UStruct>, FBlacklistNames> RawPropertyEditorWhitelist;

	/** Lazily-constructed combined cache of both the flattened class whitelist and struct whitelist */
	mutable TMap<TWeakObjectPtr<const UStruct>, FBlacklistNames> CachedPropertyEditorWhitelist;

	/** Get or create the cached whitelist for a specific UStruct */
	const FBlacklistNames& GetCachedWhitelistForStruct(const UStruct* Struct) const;
};
