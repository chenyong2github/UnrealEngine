// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

/** Project specific configuration for content encryption */
class FContentEncryptionConfig
{
public:

	struct FGroup
	{
		TSet<FName> PackageNames;
		bool bStageTimeOnly = false;
	};

	typedef TMap<FName, FGroup> TGroupMap;

	void AddPackage(FName InGroupName, FName InPackageName)
	{
		PackageGroups.FindOrAdd(InGroupName).PackageNames.Add(InPackageName);
	}

	void SetGroupAsStageTimeOnly(FName InGroupName, bool bInStageTimeOnly)
	{
		PackageGroups.FindOrAdd(InGroupName).bStageTimeOnly = bInStageTimeOnly;
	}

	void AddReleasedKey(FGuid InKey)
	{
		ReleasedKeys.Add(InKey);
	}

	const TGroupMap& GetPackageGroupMap() const
	{
		return PackageGroups;
	}


	const TSet<FGuid>& GetReleasedKeys() const
	{
		return ReleasedKeys;
	}

	void DissolveGroups(const TSet<FName>& InGroupsToDissolve)
	{
		for (FName GroupName : InGroupsToDissolve)
		{
			if (PackageGroups.Contains(GroupName))
			{
				PackageGroups.FindOrAdd(NAME_None).PackageNames.Append(PackageGroups.Find(GroupName)->PackageNames);
				PackageGroups.Remove(GroupName);
			}
		}
	}

private:

	TGroupMap PackageGroups;
	TSet<FGuid> ReleasedKeys;
};