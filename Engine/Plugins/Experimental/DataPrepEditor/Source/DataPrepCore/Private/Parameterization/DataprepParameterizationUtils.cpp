// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Parameterization/DataprepParameterizationUtils.h"

#include "DataPrepAsset.h"
#include "DataPrepOperation.h"
#include "SelectionSystem/DataprepFetcher.h"
#include "SelectionSystem/DataprepFilter.h"

#include "PropertyHandle.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

namespace DataprepParameterizationUtils
{
	bool IsAContainerProperty(UObject* Property)
	{
		if ( Property )
		{
			if ( UClass* PropertyClass = Property->GetClass() )
			{
				if ( PropertyClass == UArrayProperty::StaticClass()
					|| PropertyClass == USetProperty::StaticClass()
					|| PropertyClass == UMapProperty::StaticClass() )
				{
					return true;
				}
			}
		}

		return false;
	}

	bool IsASupportedClassForParameterization(UClass* Class)
	{
		return Class == UDataprepOperation::StaticClass()
			|| Class == UDataprepFilter::StaticClass()
			|| Class == UDataprepFetcher::StaticClass();
	}

}

uint32 GetTypeHash(const FDataprepPropertyLink& PropertyLink)
{
	return HashCombine( GetTypeHash( PropertyLink.PropertyName ), GetTypeHash( PropertyLink.ContainerIndex ) );
}

TArray<FDataprepPropertyLink> FDataprepParameterizationUtils::MakePropertyChain(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	TArray<FDataprepPropertyLink> PropertyChain;
	
	if ( PropertyHandle.IsValid() )
	{ 
		TSharedPtr<IPropertyHandle> CurrentHandle = PropertyHandle;
		UProperty* Property = CurrentHandle->GetProperty();

		TSharedPtr<IPropertyHandle> ParentHandle;

		while ( CurrentHandle.IsValid() &&  Property )
		{
			bool bWasProccess = false;
			ParentHandle = CurrentHandle->GetParentHandle();
				
			if ( ParentHandle.IsValid() )
			{ 
				UProperty* ParentProperty = ParentHandle->GetProperty();

				// We manipulate a bit the chain to store the property inside a container in a special way. So that we can 
				if ( DataprepParameterizationUtils::IsAContainerProperty( ParentProperty ) )
				{
					PropertyChain.Emplace( Property, Property->GetFName(), 0);
					PropertyChain.Emplace( ParentProperty, ParentProperty->GetFName(), CurrentHandle->GetIndexInArray() );
					CurrentHandle = ParentHandle->GetParentHandle();
					bWasProccess = true;
				}
			}

			if ( !bWasProccess )
			{
				PropertyChain.Emplace( Property, Property->GetFName(), CurrentHandle->GetIndexInArray() );
				CurrentHandle = CurrentHandle->GetParentHandle();
			}

			if ( CurrentHandle )
			{ 
				Property = CurrentHandle->GetProperty();
			}
		}
		
	}

	// Reverse the array to be from top property to bottom
	int32 Lower = 0;
	int32 Top = PropertyChain.Num() - 1;
	while ( Lower < Top )
	{
		PropertyChain.Swap( Lower, Top );
		Lower++;
		Top--;
	}

	return PropertyChain;
}

UDataprepAsset* FDataprepParameterizationUtils::GetDataprepAssetForParameterization(UObject* Object)
{
	if ( Object )
	{
		// 1. Check if the object class is part the dataprep parameterization ecosystem
		UClass* Class = Object->GetClass();
		while ( Class )
		{
			if ( DataprepParameterizationUtils::IsASupportedClassForParameterization( Class ) )
			{
				break;
			}

			Class = Class->GetSuperClass();
		}

		// 2. Check if the object inside a dataprep asset
		if ( Class )
		{
			Object = Object->GetOuter();
			const UClass* DataprepAssetClass = UDataprepAsset::StaticClass();
			while ( Object )
			{
				if ( Object->GetClass() == DataprepAssetClass )
				{
					// 3. Return the dataprep asset that own the object
					return static_cast<UDataprepAsset*>( Object );
				}

				Object = Object->GetOuter();
			}
		}
	}

	return nullptr;
}