// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowOverrideNode.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowArchive.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowOverrideNode)

template<class T>
static const T FindOverrideMapProperty(const UObject* Owner, FName PropertyName, FName ArrayKey, const T& Default = T())
{
	if (Owner && Owner->GetClass())
	{
		if (FMapProperty* MapProperty = FindFProperty<FMapProperty>(Owner->GetClass(), PropertyName))
		{
			if (const TMap<FString, FString>* ContainerData = MapProperty->ContainerPtrToValuePtr<TMap<FString, FString>>(Owner))
			{
				const FString Key = ArrayKey.ToString();
				if (ContainerData->Contains(Key))
				{
					return (FString)(*(ContainerData->Find(Key)));
				}
			}
		}
	}
	return Default;
}

bool FDataflowOverrideNode::ShouldInvalidate(FName InKey) const
{
	if (IsConnected<FName>(&Key))
	{
		return true;
	}

	return InKey == Key;
}

FString FDataflowOverrideNode::GetDefaultValue(Dataflow::FContext& Context) const
{
	return GetValue<FString>(Context, &Default, Default);
}

FString FDataflowOverrideNode::GetValueFromAsset(Dataflow::FContext& Context, const UObject* InOwner) const
{
	FName InKey = GetValue<FName>(Context, &Key, Key);
	FString EmptyString;

	if (InOwner)
	{
		if (!InKey.IsNone() && InKey.IsValid() && InKey.ToString().Len() > 0)
		{
			return FindOverrideMapProperty<FString>(InOwner, FName("Overrides"), InKey, EmptyString);
		}
	}

	return EmptyString;
}
