// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Net/UnrealNetwork.h"

FPreReplayScrub FNetworkReplayDelegates::OnPreScrub;
FOnWriteGameSpecificDemoHeader FNetworkReplayDelegates::OnWriteGameSpecificDemoHeader;
FOnProcessGameSpecificDemoHeader FNetworkReplayDelegates::OnProcessGameSpecificDemoHeader;

// ----------------------------------------------------------------

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void RegisterReplicatedLifetimeProperty(
	const UProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	ELifetimeCondition InCondition,
	ELifetimeRepNotifyCondition InRepNotifyCondition)
{
	FDoRepLifetimeParams Params;
	Params.Condition = InCondition;
	Params.RepNotifyCondition = InRepNotifyCondition;
	RegisterReplicatedLifetimeProperty(ReplicatedProperty, OutLifetimeProps, Params);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void RegisterReplicatedLifetimeProperty(
	const UProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	const FDoRepLifetimeParams& Params)
{
	if (!ReplicatedProperty) 
	{
		check(false);
		return;
	}

	for ( int32 i = 0; i < ReplicatedProperty->ArrayDim; i++ )
	{
		const uint16 RepIndex = ReplicatedProperty->RepIndex + i;
		FLifetimeProperty* RegisteredPropertyPtr = OutLifetimeProps.FindByPredicate([&RepIndex](const FLifetimeProperty& Var) { return Var.RepIndex == RepIndex; });

		FLifetimeProperty LifetimeProp(RepIndex, Params.Condition, Params.RepNotifyCondition);

		if (RegisteredPropertyPtr)
		{
			// Disabled properties can be re-enabled via DOREPLIFETIME
			if (RegisteredPropertyPtr->Condition == COND_Never)
			{
				// Copy the new conditions since disabling a property doesn't set other conditions.
				(*RegisteredPropertyPtr) = LifetimeProp;
			}
			else
			{
				// Conditions should be identical when calling DOREPLIFETIME twice on the same variable.
				checkf((*RegisteredPropertyPtr) == LifetimeProp, TEXT("Property %s was registered twice with different conditions (old:%d) (new:%d)"), *ReplicatedProperty->GetName(), RegisteredPropertyPtr->Condition, Params.Condition);
			}
		}
		else
		{
			OutLifetimeProps.Add(LifetimeProp);
		}
	}
}

void SetReplicatedPropertyToDisabled(const UProperty* ReplicatedProperty, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	check(ReplicatedProperty);

	for (int32 i = 0; i < ReplicatedProperty->ArrayDim; i++)
	{
		const uint16 RepIndex = ReplicatedProperty->RepIndex + i;
		FLifetimeProperty* RegisteredPropertyPtr = OutLifetimeProps.FindByPredicate([&RepIndex](const FLifetimeProperty& Var) { return Var.RepIndex == RepIndex; });

		if (RegisteredPropertyPtr)
		{
			RegisteredPropertyPtr->Condition = COND_Never;
		}
		else
		{
			OutLifetimeProps.Add(FLifetimeProperty(RepIndex, COND_Never));
		}
	}
}

void DisableReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	const UProperty* ReplicatedProperty = GetReplicatedProperty(ThisClass, PropertyClass, PropertyName);
	if (!ReplicatedProperty)
	{
		return;
	}

	SetReplicatedPropertyToDisabled(ReplicatedProperty, OutLifetimeProps);
}

void ResetReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, ELifetimeCondition LifetimeCondition, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	const UProperty* ReplicatedProperty = GetReplicatedProperty(ThisClass, PropertyClass, PropertyName);
	if (!ReplicatedProperty)
	{
		return;
	}

	for (int32 i = 0; i < ReplicatedProperty->ArrayDim; i++)
	{
		uint16 RepIndex = ReplicatedProperty->RepIndex + i;
		FLifetimeProperty* RegisteredPropertyPtr = OutLifetimeProps.FindByPredicate([&RepIndex](const FLifetimeProperty& Var) { return Var.RepIndex == RepIndex; });

		// Set the new condition
		if (RegisteredPropertyPtr)
		{
			RegisteredPropertyPtr->Condition = LifetimeCondition;
		}
		else
		{
			OutLifetimeProps.Add(FLifetimeProperty(RepIndex, LifetimeCondition));
		}
	}
}

void DisableAllReplicatedPropertiesOfClass(const UClass* ThisClass, const UClass* ClassToDisable, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	if (!ThisClass->IsChildOf(ClassToDisable))
	{
		ensureMsgf(false, TEXT("Attempting to disable replicated properties of '%s' but current class '%s' is not a child of '%s'"), *ClassToDisable->GetName(), *ThisClass->GetName(), *ClassToDisable->GetName());
		return;
	}

	for (TFieldIterator<UProperty> It(ClassToDisable, SuperClassBehavior); It; ++It)
	{
		const UProperty* Prop = *It;
		if (Prop && Prop->PropertyFlags & CPF_Net)
		{
			SetReplicatedPropertyToDisabled(Prop, OutLifetimeProps);
		}
	}
}

void DeprecatedChangeCondition(const  UProperty* ReplicatedProperty, TArray<FLifetimeProperty>& OutLifetimeProps, ELifetimeCondition InCondition)
{
	bool bFound = false;
	for (int32 i = 0; i < OutLifetimeProps.Num(); i++)
	{
		if (OutLifetimeProps[i].RepIndex == ReplicatedProperty->RepIndex)
		{
			for ( int32 j = 0; j < ReplicatedProperty->ArrayDim; j++ )
			{
				OutLifetimeProps[i + j].Condition = InCondition;
			}
			bFound = true;
			break;
		}
	}
	check( bFound );
}