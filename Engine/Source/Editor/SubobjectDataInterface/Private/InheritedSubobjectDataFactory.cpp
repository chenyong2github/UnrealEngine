// Copyright Epic Games, Inc. All Rights Reserved.

#include "InheritedSubobjectDataFactory.h"
#include "InheritedSubobjectData.h"

TSharedPtr<FSubobjectData> FInheritedSubobjectDataFactory::CreateSubobjectData(const FCreateSubobjectParams& Params)
{
	return TSharedPtr<FInheritedSubobjectData>(new FInheritedSubobjectData(Params.Context, Params.ParentHandle, Params.bIsInheritedSCS));
}

bool FInheritedSubobjectDataFactory::ShouldCreateSubobjectData(const FCreateSubobjectParams& Params) const
{
	if(UActorComponent* Component = Cast<UActorComponent>(Params.Context))
	{
		// Create an inherited subobject data
		if(Params.bIsInheritedSCS || Component->CreationMethod == EComponentCreationMethod::Native)
		{
			return true;
		}
	}

	return false;
}