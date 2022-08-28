// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// NOTE: ugly hack to register custom attribute without calling FMaterialAttributeDefinitionMap::AddCustomAttribute (which doesn't work after the AttributeDDCString has been initialized)
void AddCustomAttributeHack(const FGuid& AttributeID, const FString& AttributeName, const FString& FunctionName, EMaterialValueType ValueType, const FVector4& DefaultValue, MaterialAttributeBlendFunction BlendFunction = nullptr);
