// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Binding/DynamicPropertyPath.h"
#include "PropertyPathHelpers.h"

FDynamicPropertyPath::FDynamicPropertyPath()
{
}

FDynamicPropertyPath::FDynamicPropertyPath(const FString& Path)
	: FCachedPropertyPath(Path)
{
}

FDynamicPropertyPath::FDynamicPropertyPath(const TArray<FString>& PropertyChain)
	: FCachedPropertyPath(TEXT(""))
{
	FString PropertyPath;

	for (const FString& Segment : PropertyChain)
	{
		if (PropertyPath.IsEmpty())
		{
			PropertyPath = Segment;
		}
		else
		{
			PropertyPath = PropertyPath + TEXT(".") + Segment;
		}
	}

	*this = FDynamicPropertyPath(PropertyPath);
}
