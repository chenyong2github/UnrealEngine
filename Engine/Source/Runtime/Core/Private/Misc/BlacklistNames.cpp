// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/BlacklistNames.h"
#include "Misc/StringBuilder.h"

FBlacklistNames::FBlacklistNames() :
	bSuppressOnFilterChanged(false)
{
}

bool FBlacklistNames::PassesFilter(const FName Item) const
{
	if (Whitelist.Num() > 0 && !Whitelist.Contains(Item))
	{
		return false;
	}

	if (Blacklist.Contains(Item))
	{
		return false;
	}

	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	return true;
}

void FBlacklistNames::AddBlacklistItem(const FName OwnerName, const FName Item)
{
	Blacklist.FindOrAdd(Item).AddUnique(OwnerName);
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistNames::AddWhitelistItem(const FName OwnerName, const FName Item)
{
	Whitelist.FindOrAdd(Item).AddUnique(OwnerName);	
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistNames::AddBlacklistAll(const FName OwnerName)
{
	BlacklistAll.AddUnique(OwnerName);

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

bool FBlacklistNames::HasFiltering() const
{
	return Blacklist.Num() > 0 || Whitelist.Num() > 0 || BlacklistAll.Num() > 0;
}

void FBlacklistNames::UnregisterOwner(const FName OwnerName)
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

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistNames::Append(const FBlacklistNames& Other)
{
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

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

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

/** FBlacklistPaths */
FBlacklistPaths::FBlacklistPaths() :
	bSuppressOnFilterChanged(false)
{
}

bool FBlacklistPaths::PassesFilter(const FStringView Item) const
{
	const uint32 ItemHash = GetTypeHash(Item);

	if (Whitelist.Num() > 0 && !Whitelist.ContainsByHash(ItemHash, Item))
	{
		return false;
	}

	if (Blacklist.ContainsByHash(ItemHash, Item))
	{
		return false;
	}

	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	return true;
}

bool FBlacklistPaths::PassesFilter(const FName Item) const
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return PassesFilter(FStringView(ItemStr));
}

bool FBlacklistPaths::PassesFilter(const TCHAR* Item) const
{
	return PassesFilter(FStringView(Item));
}

bool FBlacklistPaths::PassesStartsWithFilter(const FStringView Item) const
{
	if (Whitelist.Num() > 0)
	{
		bool bPassedWhitelist = false;
		for (const auto& Other : Whitelist)
		{
			if (Item.StartsWith(Other.Key) && (Item.Len() <= Other.Key.Len() || Item[Other.Key.Len()] == TEXT('/')))
			{
				bPassedWhitelist = true;
				break;
			}
		}

		if (!bPassedWhitelist)
		{
			return false;
		}
	}

	if (Blacklist.Num() > 0)
	{
		for (const auto& Other : Blacklist)
		{
			if (Item.StartsWith(Other.Key) && (Item.Len() <= Other.Key.Len() || Item[Other.Key.Len()] == TEXT('/')))
			{
				return false;
			}
		}
	}

	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	return true;
}

bool FBlacklistPaths::PassesStartsWithFilter(const FName Item) const
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return PassesStartsWithFilter(FStringView(ItemStr));
}

bool FBlacklistPaths::PassesStartsWithFilter(const TCHAR* Item) const
{
	return PassesStartsWithFilter(FStringView(Item));
}

void FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const FStringView Item)
{
	const uint32 ItemHash = GetTypeHash(Item);

	if (FBlacklistOwners* FoundOwners = Blacklist.FindByHash(ItemHash, Item))
	{
		FoundOwners->AddUnique(OwnerName);
	}
	else
	{
		Blacklist.AddByHash(ItemHash, FString(Item)).Add(OwnerName);
	}
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const FName Item)
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return AddBlacklistItem(OwnerName, FStringView(ItemStr));
}

void FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const TCHAR* Item)
{
	return AddBlacklistItem(OwnerName, FStringView(Item));
}

void FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const FStringView Item)
{
	const uint32 ItemHash = GetTypeHash(Item);

	if (FBlacklistOwners* FoundOwners = Whitelist.FindByHash(ItemHash, Item))
	{
		FoundOwners->AddUnique(OwnerName);
	}
	else
	{
		Whitelist.AddByHash(ItemHash, FString(Item)).Add(OwnerName);
	}
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const FName Item)
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return AddWhitelistItem(OwnerName, FStringView(ItemStr));
}

void FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const TCHAR* Item)
{
	return AddWhitelistItem(OwnerName, FStringView(Item));
}

void FBlacklistPaths::AddBlacklistAll(const FName OwnerName)
{
	BlacklistAll.AddUnique(OwnerName);

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

bool FBlacklistPaths::HasFiltering() const
{
	return Blacklist.Num() > 0 || Whitelist.Num() > 0 || BlacklistAll.Num() > 0;
}

void FBlacklistPaths::UnregisterOwner(const FName OwnerName)
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

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistPaths::Append(const FBlacklistPaths& Other)
{
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

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

	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}
