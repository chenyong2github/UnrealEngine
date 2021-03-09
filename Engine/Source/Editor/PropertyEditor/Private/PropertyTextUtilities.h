// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

class FPropertyNode;
class FObjectBaseAddress;

class FPropertyTextUtilities
{
public:
	static void PropertyToTextHelper(FString& OutString, const FPropertyNode* InPropertyNode, const FProperty* Property, uint8* ValueAddress, EPropertyPortFlags PortFlags);
	static void PropertyToTextHelper(FString& OutString, const FPropertyNode* InPropertyNode, const FProperty* Property, const FObjectBaseAddress& ObjectAddress, EPropertyPortFlags PortFlags);
	static void TextToPropertyHelper(const TCHAR* Buffer, const FPropertyNode* InPropertyNode, const FProperty* Property, uint8* ValueAddress, UObject* Object, EPropertyPortFlags PortFlags = PPF_None);
	static void TextToPropertyHelper(const TCHAR* Buffer, const FPropertyNode* InPropertyNode, const FProperty* Property, const FObjectBaseAddress& ObjectAddress, EPropertyPortFlags PortFlags = PPF_None);
};