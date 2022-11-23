// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OptimusSource)

void UOptimusSource::SetSource(const FString& InText)
{
	SourceText = InText;
	
	Modify();
}

#if WITH_EDITOR	

FString UOptimusSource::GetNameForShaderTextEditor() const
{
	return GetFName().ToString();
}

#endif // WITH_EDITOR	
