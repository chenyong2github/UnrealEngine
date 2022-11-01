// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserPropertyAccess.h"
#include "UObject/UnrealType.h"

namespace UE::Chooser
{
	bool ResolvePropertyChain(const void*& Container, UStruct*& StructType, const TArray<FName>& PropertyBindingChain)
	{
		if (PropertyBindingChain.Num() == 0)
		{
			return false;
		}
	
		const int PropertyChainLength = PropertyBindingChain.Num();
		for(int PropertyChainIndex = 0; PropertyChainIndex < PropertyChainLength - 1; PropertyChainIndex++)
		{
			if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(StructType, PropertyBindingChain[PropertyChainIndex]))
			{
				StructType = StructProperty->Struct;
				Container = StructProperty->ContainerPtrToValuePtr<void>(Container);
			}
			else if (const FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(StructType, PropertyBindingChain[PropertyChainIndex]))
			{
				StructType = ObjectProperty->PropertyClass;
				Container = *ObjectProperty->ContainerPtrToValuePtr<TObjectPtr<UObject>>(Container);
				if (Container == nullptr)
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
	
		return true;
	}
}