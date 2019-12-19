// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Net/UnrealNetwork.h"

FPreReplayScrub FNetworkReplayDelegates::OnPreScrub;
FOnWriteGameSpecificDemoHeader FNetworkReplayDelegates::OnWriteGameSpecificDemoHeader;
FOnProcessGameSpecificDemoHeader FNetworkReplayDelegates::OnProcessGameSpecificDemoHeader;
FOnWriteGameSpecificFrameData FNetworkReplayDelegates::OnWriteGameSpecificFrameData;
FOnProcessGameSpecificFrameData FNetworkReplayDelegates::OnProcessGameSpecificFrameData;

// ----------------------------------------------------------------

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void RegisterReplicatedLifetimeProperty(
	const FProperty* ReplicatedProperty,
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
	const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	const FDoRepLifetimeParams& Params)
{
	for (int32 i = 0; i < PropertyDescriptor.ArrayDim; i++)
	{
		const uint16 RepIndex = PropertyDescriptor.RepIndex + i;
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
				checkf((*RegisteredPropertyPtr) == LifetimeProp, TEXT("Property %s was registered twice with different conditions (old:%d) (new:%d)"), PropertyDescriptor.PropertyName, RegisteredPropertyPtr->Condition, Params.Condition);
			}
		}
		else
		{
			OutLifetimeProps.Add(LifetimeProp);
		}
	}
}

void RegisterReplicatedLifetimeProperty(
	const FProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	const FDoRepLifetimeParams& Params)
{
	if (ReplicatedProperty == nullptr)
	{
		check(false);
		return;
	}

	RegisterReplicatedLifetimeProperty(NetworkingPrivate::FRepPropertyDescriptor(ReplicatedProperty), OutLifetimeProps, Params);
}

void SetReplicatedPropertyToDisabled(const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor, TArray<FLifetimeProperty>& OutLifetimeProps)
{
	for (int32 i = 0; i < PropertyDescriptor.ArrayDim; i++)
	{
		const uint16 RepIndex = PropertyDescriptor.RepIndex + i;
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

void SetReplicatedPropertyToDisabled(const FProperty* ReplicatedProperty, TArray<FLifetimeProperty>& OutLifetimeProps)
{
	SetReplicatedPropertyToDisabled(NetworkingPrivate::FRepPropertyDescriptor(ReplicatedProperty), OutLifetimeProps);
}

void DisableReplicatedLifetimeProperty(const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor, TArray<FLifetimeProperty>& OutLifetimeProps)
{
	SetReplicatedPropertyToDisabled(PropertyDescriptor, OutLifetimeProps);
}

void DisableReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	const FProperty* ReplicatedProperty = GetReplicatedProperty(ThisClass, PropertyClass, PropertyName);
	if (!ReplicatedProperty)
	{
		return;
	}

	SetReplicatedPropertyToDisabled(NetworkingPrivate::FRepPropertyDescriptor(ReplicatedProperty), OutLifetimeProps);
}

void ResetReplicatedLifetimeProperty(
	const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor,
	ELifetimeCondition LifetimeCondition,
	TArray<FLifetimeProperty>& OutLifetimeProps)
{
	for (int32 i = 0; i < PropertyDescriptor.ArrayDim; i++)
	{
		uint16 RepIndex = PropertyDescriptor.RepIndex + i;
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

void ResetReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, ELifetimeCondition LifetimeCondition, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	const FProperty* ReplicatedProperty = GetReplicatedProperty(ThisClass, PropertyClass, PropertyName);
	if (!ReplicatedProperty)
	{
		return;
	}

	ResetReplicatedLifetimeProperty(NetworkingPrivate::FRepPropertyDescriptor(ReplicatedProperty), LifetimeCondition, OutLifetimeProps);
}

void DisableAllReplicatedPropertiesOfClass(const NetworkingPrivate::FRepClassDescriptor& ClassDescriptor, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray<FLifetimeProperty>& OutLifetimeProps)
{
	const int32 StartIndex = (EFieldIteratorFlags::IncludeSuper == SuperClassBehavior) ? 0 : ClassDescriptor.StartRepIndex;
	for (int32 RepIndex = StartIndex; RepIndex < ClassDescriptor.EndRepIndex; ++RepIndex)
	{
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

void DisableAllReplicatedPropertiesOfClass(const UClass* ThisClass, const UClass* ClassToDisable, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray< FLifetimeProperty >& OutLifetimeProps)
{
	if (!ThisClass->IsChildOf(ClassToDisable))
	{
		ensureMsgf(false, TEXT("Attempting to disable replicated properties of '%s' but current class '%s' is not a child of '%s'"), *ClassToDisable->GetName(), *ThisClass->GetName(), *ClassToDisable->GetName());
		return;
	}

	for (TFieldIterator<FProperty> It(ClassToDisable, SuperClassBehavior); It; ++It)
	{
		const FProperty* Prop = *It;
		if (Prop && Prop->PropertyFlags & CPF_Net)
		{
			SetReplicatedPropertyToDisabled(Prop, OutLifetimeProps);
		}
	}
}


void DeprecatedChangeCondition(const  FProperty* ReplicatedProperty, TArray<FLifetimeProperty>& OutLifetimeProps, ELifetimeCondition InCondition)
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