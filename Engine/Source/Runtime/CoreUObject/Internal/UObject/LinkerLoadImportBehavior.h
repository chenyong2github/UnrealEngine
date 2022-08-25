// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/ObjectHandle.h"

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
#define UE_API COREUOBJECT_API

class FLinkerLoad;
struct FObjectImport;

namespace UE::LinkerLoad
{
enum class EImportBehavior : uint8
{
	Eager = 0,
	// @TODO: OBJPTR: we want to permit lazy background loading in the future
	//LazyBackground,
	LazyOnDemand,
};


EImportBehavior GetPropertyImportLoadBehavior(const FObjectImport& Import, const FLinkerLoad& LinkerLoad);
}

#undef UE_API
#endif // UE_WITH_OBJECT_HANDLE_LATE_RESOLVE
