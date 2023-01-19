// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSource.h"
#include "OptimusHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OptimusSource)

void UOptimusSource::SetSource(const FString& InText)
{
	SourceText = InText;
	
	Modify();
}


FString UOptimusSource::GetSource() const
{
	FString ShaderPathName = GetPathName();
	Optimus::ConvertObjectPathToShaderFilePath(ShaderPathName);
	return FString::Printf(TEXT("#line 1 \"%s\"\n%s"), *ShaderPathName, *SourceText);
}

#if WITH_EDITOR	

FString UOptimusSource::GetNameForShaderTextEditor() const
{
	return GetFName().ToString();
}

#endif // WITH_EDITOR	
