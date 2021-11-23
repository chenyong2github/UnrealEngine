// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMDeveloperModule.h"

namespace RigVMPythonUtils
{
	RIGVMDEVELOPER_API FString NameToPep8(const FString& Name);

	RIGVMDEVELOPER_API FString TransformToPythonString(const FTransform& Transform);

	RIGVMDEVELOPER_API FString Vector2DToPythonString(const FVector2D& Vector);

	RIGVMDEVELOPER_API FString LinearColorToPythonString(const FLinearColor& Color);
	
#if WITH_EDITOR
	RIGVMDEVELOPER_API void Print(const FString& BlueprintTitle, const FString& InMessage);

	RIGVMDEVELOPER_API void PrintPythonContext(const FString& InBlueprintName);

#endif
}
