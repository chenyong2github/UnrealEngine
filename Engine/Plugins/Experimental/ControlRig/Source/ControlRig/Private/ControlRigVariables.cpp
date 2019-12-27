// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigVariables.h"
#include "PropertyPathHelpers.h"

// copy of used types from const UEdGraphSchema_K2 - they're only editor only
namespace FControlRigIOTypes
{
	const FName CR_Boolean(TEXT("bool"));
	const FName CR_Byte(TEXT("byte"));
	const FName CR_Int(TEXT("int"));
	const FName CR_Int64(TEXT("int64"));
	const FName CR_Float(TEXT("float"));
	const FName CR_Name(TEXT("name"));
	const FName CR_Struct(TEXT("struct"));
}


