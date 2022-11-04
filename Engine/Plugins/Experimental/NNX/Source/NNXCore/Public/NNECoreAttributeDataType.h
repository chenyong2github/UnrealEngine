// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/Object.h"

#include "NNECoreAttributeDataType.generated.h"

/**
 * Attribute data types
 * 
 * Note: also extend NNEAttributeValueTraits.h for more type support
 */
UENUM()
enum class ENNEAttributeDataType : uint8
{
	None,
	Float,								//!< 32-bit floating number
	Int32								//!< 32-bit signed integer
	//TODO add more AttributeDataType support
};

/**
 * @return ENNEAttributeDataType from the string passed in
 */
inline void LexFromString(ENNEAttributeDataType& OutValue, const TCHAR* StringVal)
{
	int64 EnumVal = StaticEnum<ENNEAttributeDataType>()->GetValueByName(StringVal);
	if (EnumVal == INDEX_NONE)
	{
		OutValue = ENNEAttributeDataType::None;
		ensureMsgf(false, TEXT("ENNEAttributeDataType LexFromString didn't have a match for '%s'"), StringVal);
	}

	OutValue = (ENNEAttributeDataType)EnumVal;
};