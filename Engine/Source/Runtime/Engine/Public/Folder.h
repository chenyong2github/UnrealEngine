// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Paths.h"

struct FFolder
{
	typedef FObjectKey FRootObject;

	FFolder(const FName& InPath = NAME_None, const FRootObject& InRootObject = GetDefaultRootObject())
		: Path(InPath)
		, RootObject(InRootObject)
	{}

	FORCEINLINE static bool HasRootObject(const FRootObject& Key)
	{
		return Key != GetDefaultRootObject();
	}

	FORCEINLINE static FRootObject GetDefaultRootObject()
	{
		return FRootObject();
	}

	FORCEINLINE bool HasRootObject() const
	{
		return FFolder::HasRootObject(RootObject);
	}

	FORCEINLINE FFolder GetParent() const
	{
		const FName ParentPath(*FPaths::GetPath(Path.ToString()));
		return FFolder(ParentPath, RootObject);
	}

	FORCEINLINE bool IsChildOf(const FFolder& InParent) const
	{
		if (RootObject != InParent.RootObject)
		{
			return false;
		}
		return PathIsChildOf(GetPath(), InParent.GetPath());
	}

	FORCEINLINE bool IsNone() const
	{
		return Path.IsNone();
	}

	const FRootObject& GetRootObject() const
	{
		return RootObject;
	}

	FORCEINLINE UObject* GetRootObjectPtr() const
	{
		return RootObject.ResolveObjectPtr();
	}

	FORCEINLINE const FName& GetPath() const
	{
		return Path;
	}

	FORCEINLINE void SetPath(const FName& InPath)
	{
		Path = InPath;
	}

	FORCEINLINE FName GetLeafName() const
	{
		FString PathString = Path.ToString();
		int32 LeafIndex = 0;
		if (PathString.FindLastChar('/', LeafIndex))
		{
			return FName(*PathString.RightChop(LeafIndex + 1));
		}
		else
		{
			return Path;
		}
	}

	FORCEINLINE bool operator == (const FFolder& InOther) const
	{
		return (Path == InOther.Path) && (RootObject == InOther.RootObject);
	}

	FORCEINLINE bool operator != (const FFolder& InOther) const
	{
		return !operator==(InOther);
	}

	FORCEINLINE FString ToString() const
	{
		return Path.ToString();
	}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FFolder& Folder)
	{
		check(!Ar.IsPersistent());
		return Ar << Folder.Path << Folder.RootObject;
	}

private:
	bool PathIsChildOf(const FString& InPotentialChild, const FString& InParent) const
	{
		const int32 ParentLen = InParent.Len();
		
		// If parent is empty and child isn't, consider that path is child of parent
		if ((InPotentialChild.Len() > 0) && (ParentLen == 0))
		{
			return true;
		}

		return
			InPotentialChild.Len() > ParentLen &&
			InPotentialChild[ParentLen] == '/' &&
			InPotentialChild.Left(ParentLen) == InParent;
	}

	bool PathIsChildOf(const FName& InPotentialChild, const FName& InParent) const
	{
		return PathIsChildOf(InPotentialChild.ToString(), InParent.ToString());
	}

	FName Path;
	FRootObject RootObject;
};

FORCEINLINE uint32 GetTypeHash(const FFolder& InFolder)
{
	return HashCombine(GetTypeHash(InFolder.GetPath()), GetTypeHash(InFolder.GetRootObject()));
}