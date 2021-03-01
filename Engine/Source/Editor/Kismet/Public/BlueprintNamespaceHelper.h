// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Utility class for extracting namespace info from a specified Blueprint context.
class KISMET_API FBlueprintNamespaceHelper
{
public:
	explicit FBlueprintNamespaceHelper(const class UBlueprint* InBlueprint);

	void AddNamespace(const FString& Namespace)
	{
		if (!Namespace.IsEmpty())
		{
			FullyQualifiedListOfNamespaces.Add(Namespace);
		}
	}

	template<typename ContainerType>
	void AddNamespaces(const ContainerType& NamespaceList)
	{
		if (NamespaceList.Num() > 0)
		{
			FullyQualifiedListOfNamespaces.Reserve(FullyQualifiedListOfNamespaces.Num() + NamespaceList.Num());
			for (const FString& Namespace : NamespaceList)
			{
				AddNamespace(Namespace);
			}
		}
	}

	bool IsIncludedInNamespaceList(const FString& TestNamespace) const;

	bool IsImportedType(const UField* InType) const;
	bool IsImportedObject(const UObject* InObject) const;
	bool IsImportedObject(const FSoftObjectPath& InObjectPath) const;

private:
	// Complete list of all fully-qualified namespace path identifiers for the associated Blueprint.
	TSet<FString> FullyQualifiedListOfNamespaces;
};