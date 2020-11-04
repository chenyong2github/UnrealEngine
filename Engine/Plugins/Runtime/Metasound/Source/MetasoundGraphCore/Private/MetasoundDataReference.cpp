// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundDataReference.h"

namespace Metasound
{
	const TCHAR* TDataReferenceTypeInfo<void>::TypeName = TEXT("void");
	const void* const TDataReferenceTypeInfo<void>::TypePtr = nullptr;
	const void* const TDataReferenceTypeInfo<void>::TypeId = static_cast<const void* const>(TDataReferenceTypeInfo<void>::TypePtr);
}
