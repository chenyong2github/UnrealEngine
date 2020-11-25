// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Definition 
 *
 *  https://docs.google.com/document/d/1yd8GCfT2aufxSdb5jAzlNTr1SptxEFpS9pWdQY-8LIk/edit#
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigHierarchy.generated.h"

struct FIKRigTransform;

USTRUCT()
struct IKRIG_API FIKRigBone 
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = FIKRigBone)
	FName Name;

	// more user friendly with name than Index, and easy to move to different location without losing who was the parent
	UPROPERTY(VisibleAnywhere, Category = FIKRigBone)
	FName ParentName;

	FIKRigBone(FName InName = NAME_None, FName InParentName = NAME_None)
		: Name(InName)
		, ParentName(InParentName)
	{
	}

private:
	// these are built when rebuilt
	// don't rely on them in editor
	UPROPERTY(transient)
	TArray<int32> Children;

	friend FIKRigHierarchy;
};

USTRUCT()
struct IKRIG_API FIKRigHierarchy
{
	GENERATED_BODY()

private:
	UPROPERTY(VisibleAnywhere, Category=FIKRigHierarchy)
	TArray<FIKRigBone> Bones;

	// cached data for runtime is here, so that it can be shared by all the instances
	// this should be required to build only once during runtime
	UPROPERTY(transient)
	TArray<int32> ParentIndices;

	// from name to index of Relationship
	UPROPERTY(transient)
	TMap<FName, int32> RuntimeNameLookupTable;

	/***** runtime operators  ****/
	// rebuild cache data if required
	void RebuildCacheData()
	{
		RuntimeNameLookupTable.Reset();
		ParentIndices.Reset();

		if (Bones.Num() > 0)
		{
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				RuntimeNameLookupTable.Add(Bones[Index].Name) = Index;
				Bones[Index].Children = FindChildren(Index);
			}

			ParentIndices.AddUninitialized(Bones.Num());
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				if (Bones[Index].ParentName != NAME_None)
				{
					int32* ParentIndex = RuntimeNameLookupTable.Find(Bones[Index].ParentName);
					if (ParentIndex)
					{
						ParentIndices[Index] = *ParentIndex;
					}
					else
					{
						// should not happen
						ensureMsgf(false, TEXT("IKRig : [%s]'s parent [%s] not found. Suspect data issue."), *Bones[Index].Name.ToString(), *Bones[Index].ParentName.ToString());
						ParentIndices[Index] = INDEX_NONE;
					}
				}
				else
				{
					ParentIndices[Index] = INDEX_NONE;
				}
			}
		}

	}

public: 
	bool IsValidIndex(int32 Index) const
	{
		return Bones.IsValidIndex(Index);
	}

	int32 GetNum() const
	{
		return Bones.Num();
	}

	int32 GetParentIndex(int32 Index) const
	{
		ValidateRuntimeData();

		if (ParentIndices.IsValidIndex(Index))
		{
			return ParentIndices[Index];
		}

		return INDEX_NONE;
	}

	int32 GetParentIndex(const FName& InName) const
	{
		ValidateRuntimeData();

		if (InName != NAME_None)
		{
			const int32* Found = RuntimeNameLookupTable.Find(InName);
			if (Found)
			{
				return GetParentIndex(*Found);
			}
		}

		return INDEX_NONE;
	}

	int32 GetIndex(const FName& InName) const
	{
		ValidateRuntimeData();

		if (InName != NAME_None)
		{
			const int32* Found = RuntimeNameLookupTable.Find(InName);
			if (Found)
			{
				return *Found;
			}
		}

		return INDEX_NONE;
	}

	TArray<int32> FindChildren(int32 Index) const;

private:
	void ValidateRuntimeData() const
	{
		ensure (RuntimeNameLookupTable.Num() == Bones.Num() && ParentIndices.Num() == Bones.Num());
	}

	//slow function, this is only used for editor or for cache only
	//  don't use this for runtime purpose, it'll be cached with Bones
	TArray<int32> FindIndicesByParentName(const FName& InParentName) const
	{
		TArray<int32> Indices;

		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			if (Bones[Index].ParentName == InParentName)
			{
				Indices.Add(Index);
			}
		}
		return Indices;
	}

#if WITH_EDITOR
	int32 FindIndexFromBoneArray(const FName& InName) const
	{
		return Bones.IndexOfByPredicate([&](const FIKRigBone& Item) { return Item.Name == InName; });
	}
#endif// WITH_EDITOR

	friend class UIKRigDefinition;
};

