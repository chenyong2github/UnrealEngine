// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "GeometryCollection/ManagedArrayCollection.h"

/**
 * this class wraps a managed array
 * this provides a convenient API for optional attributes in a collection facade
 */
template <typename T>
struct TManagedArrayAccessor
{
public:
	TManagedArrayAccessor(FManagedArrayCollection& InCollection, const FName& AttributeName, const FName& AttributeGroup)
		: Collection(InCollection)
		, Name(AttributeName)
		, Group(AttributeGroup)
	{
		AttributeArray = InCollection.FindAttributeTyped<T>(AttributeName, AttributeGroup);

	}
	bool IsValid() const { return AttributeArray != nullptr; }

	/** get the attribute for read only */
	const TManagedArray<T>& Get() const
	{
		check(IsValid());
		return *AttributeArray;
	}

	/** get the attribute for modification */
	TManagedArray<T>& Modify() const
	{
		check(IsValid());
		AttributeArray->MarkDirty();
		return *AttributeArray;
	}

	/** add the attribute if it does not exists yet */
	TManagedArray<T>& Add()
	{
		AttributeArray = &Collection.AddAttribute<T>(Name, Group);
		return *AttributeArray;
	}

	/** add and fill the attribute if it does not exist yet */
	void AddAndFill(const T& Value)
	{
		if (!Collection.HasAttribute(Name, Group))
		{
			AttributeArray = &Collection.AddAttribute<T>(Name, Group);
			AttributeArray->Fill(Value);
		}
	}

private:
	FManagedArrayCollection& Collection;
	FName Name;
	FName Group;
	TManagedArray<T>* AttributeArray;
};
