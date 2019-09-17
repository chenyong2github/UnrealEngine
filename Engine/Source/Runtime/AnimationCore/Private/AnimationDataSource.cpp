// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationDataSource.h"

bool UAnimationDataSourceRegistry::RegisterDataSource(const FName& InName, UObject* InDataSource)
{
	if (DataSources.Contains(InName))
	{
		return false;
	}
	DataSources.Add(InName, InDataSource);
	return true;
}

bool UAnimationDataSourceRegistry::UnregisterDataSource(const FName& InName)
{
	return DataSources.Remove(InName) > 0;
}

bool UAnimationDataSourceRegistry::ContainsSource(const FName& InName) const
{
	return DataSources.Contains(InName);
}

UObject* UAnimationDataSourceRegistry::RequestSource(const FName& InName, UClass* InExpectedClass) const
{
	UObject* const* DataSource = DataSources.Find(InName);
	if (DataSource == nullptr)
	{
		return nullptr;
	}
	if (*DataSource == nullptr)
	{
		return nullptr;
	}
	if (!(*DataSource)->IsA(InExpectedClass))
	{
		return nullptr;
	}
	return *DataSource;
}