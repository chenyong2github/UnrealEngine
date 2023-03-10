// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchDatabase.h"

void UPoseSearchNormalizationSet::AddUniqueDatabases(TArray<TWeakObjectPtr<const UPoseSearchDatabase>>& UniqueDatabases) const
{
	for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
	{
		if (Database)
		{
			UniqueDatabases.AddUnique(Database);
		}
	}
}