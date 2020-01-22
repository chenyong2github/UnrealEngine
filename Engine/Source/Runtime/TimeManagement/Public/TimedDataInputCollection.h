// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class ITimedDataInput;
class ITimedDataInputGroup;

/**
 * A list of all the timed data input.
 */
class TIMEMANAGEMENT_API FTimedDataInputCollection
{
public:

	/** Add an timed input to the collection. */
	void Add(ITimedDataInput* Input)
	{
		if (Input && !Inputs.Contains(Input))
		{
			Inputs.Add(Input);
			OnCollectionChanged().Broadcast();
		}
	}

	/** Remove an input from the collection. */
	void Remove(ITimedDataInput* Input)
	{
		if (Inputs.RemoveSingleSwap(Input) > 0)
		{
			OnCollectionChanged().Broadcast();
		}
	}

	/** The list of inputs from the collection. */
	const TArray<ITimedDataInput*>& GetInputs() const
	{
		return Inputs;
	}
	
	/** Add an input group to the collection. */
	void Add(ITimedDataInputGroup* Input)
	{
		if (Input && !Groups.Contains(Input))
		{
			Groups.Add(Input);
			OnCollectionChanged().Broadcast();
		}
	}

	/** Remove an input group from the collection. */
	void Remove(ITimedDataInputGroup* Input)
	{
		if (Groups.RemoveSingleSwap(Input) > 0)
		{
			OnCollectionChanged().Broadcast();
		}
	}

	/** The list of inputs groups from the collection. */
	const TArray<ITimedDataInputGroup*>& GetGroups() const
	{
		return Groups;
	}

	/** When an element is added or removed to the collection. */
	FSimpleMulticastDelegate& OnCollectionChanged()
	{
		return CollectionChanged;
	}

private:
	FSimpleMulticastDelegate CollectionChanged;
	TArray<ITimedDataInput*> Inputs;
	TArray<ITimedDataInputGroup*> Groups;
};