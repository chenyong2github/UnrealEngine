// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSource.h"

void UOptimusSource::SetSource(const FString& InText)
{
	SourceText = InText;
	
	Modify();
}
