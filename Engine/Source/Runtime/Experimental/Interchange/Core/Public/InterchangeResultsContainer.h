// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeResult.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeResultsContainer.generated.h"


UCLASS(Experimental)
class INTERCHANGECORE_API UInterchangeResultsContainer : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Returns whether the results container is empty or not
	 */
	bool IsEmpty() const
	{
		FScopeLock ScopeLock(&Lock);
		return Results.IsEmpty();
	}

	/**
	 * Empties the results container
	 */
	void Empty();

	/**
	 * Appends the given results container to this one
	 */
	void Append(UInterchangeResultsContainer* Other);

	/**
	 * Creates a UInterchangeResult of the given type, adds it to the container and returns it.
	 */
	template <typename T>
	T* Add()
	{
		FScopeLock ScopeLock(&Lock);
		T* Item = NewObject<T>(GetTransientPackage());
		Results.Add(Item);
		return Item;
	}

	/**
	 * Adds the given UInterchangeResult to the container.
	 */
	void Add(UInterchangeResult* Item)
	{
		FScopeLock ScopeLock(&Lock);
		Results.Add(Item);
	}

	/**
	 * Finalizes the container, prior to passing it to the UI display
	 */
	void Finalize();

	/**
	 * Return the contained array (by value, for thread safety).
	 */
	TArray<UInterchangeResult*> GetResults() const
	{
		FScopeLock ScopeLock(&Lock);
		return Results;
	}

private:

	mutable FCriticalSection Lock;

	UPROPERTY()
	TArray<TObjectPtr<UInterchangeResult>> Results;
};
