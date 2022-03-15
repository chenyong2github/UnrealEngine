// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebAPIOpenAPIFactoryBase.h"

#define LOCTEXT_NAMESPACE "WebAPIOpenAPIFactoryBase"

const FName UWebAPIOpenAPIFactoryBase::JsonFileType = FName(TEXT("json"));
const FName UWebAPIOpenAPIFactoryBase::YamlFileType = FName(TEXT("yaml"));

UWebAPIOpenAPIFactoryBase::UWebAPIOpenAPIFactoryBase()
{
	Formats.Add(TEXT("json;JavaScript Object Notation"));
}

TFuture<bool> UWebAPIOpenAPIFactoryBase::ImportWebAPI(UWebAPIDefinition* InDefinition, const FString& InFileName, const FString& InFileContents)
{
	// @todo: modify definition here
	// write raw spec contents
	// parse spec

	// @todo: return false if json couldn't be parsed

	return MakeFulfilledPromise<bool>(false).GetFuture();
}

bool UWebAPIOpenAPIFactoryBase::IsValidFileExtension(const FString& InFileExtension) const
{
	return InFileExtension.Equals(JsonFileType.ToString(), ESearchCase::IgnoreCase);
}

#undef LOCTEXT_NAMESPACE
