// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/BlacklistNames.h"

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

bool FBlacklistPaths::PassesFilter(const FString& Item) const
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

bool FBlacklistPaths::PassesFilter(const FName Item) const
{
	return PassesFilter(Item.ToString());
}

bool FBlacklistPaths::PassesStartsWithFilter(const FString& Item) const
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
	return PassesStartsWithFilter(Item.ToString());
}

void FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const FString& Item)
{
	Blacklist.FindOrAdd(Item).AddUnique(OwnerName);
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
}

void FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const FString& Item)
{
	Whitelist.FindOrAdd(Item).AddUnique(OwnerName);	
	
	if (!bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}
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
