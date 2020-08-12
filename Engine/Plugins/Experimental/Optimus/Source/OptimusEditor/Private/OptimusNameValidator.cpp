// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNameValidator.h"

#include "OptimusNodeGraph.h"
#include "IOptimusNodeGraphCollectionOwner.h"


FOptimusNameValidator::FOptimusNameValidator(
	const IOptimusNodeGraphCollectionOwner* InRoot,
	FName InExistingName
	) :
	Root(InRoot),
	ExistingName(InExistingName)
{
	if (ensure(InRoot))
	{
		for (const UOptimusNodeGraph* Graph : InRoot->GetGraphs())
		{
			Names.Add(Graph->GetFName());
		}
	}
}


EValidatorResult FOptimusNameValidator::IsValid(const FString& InName, bool)
{
	if (InName.Len() >= NAME_SIZE)
	{
		return EValidatorResult::TooLong;
	}
	else if (!FName::IsValidXName(InName, InvalidCharacters()))
	{
		return EValidatorResult::ContainsInvalidCharacters;
	}

	return IsValid(FName(*InName));
}


EValidatorResult FOptimusNameValidator::IsValid(const FName& InName, bool)
{
	if (InName == NAME_None)
	{
		return EValidatorResult::EmptyName;
	}
	else if (InName == ExistingName)
	{
		return EValidatorResult::Ok;
	}
	else if (Names.Contains(InName))
	{
		return EValidatorResult::AlreadyInUse;
	}
	else
	{
		return EValidatorResult::Ok;
	}
}
