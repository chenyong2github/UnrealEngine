// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "DataLayerUtils.generated.h"

#define DATALAYER_TO_INSTANCE_RUNTIME_CONVERSION_ENABLED 1

UENUM(BlueprintType)
enum class EDataLayerType : uint8
{
	Runtime,
	Editor,
	Unknown UMETA(Hidden),

	Size UMETA(Hidden)
};

namespace DataLayerUtils
{
#if WITH_EDITOR
	constexpr const TCHAR* IconNameByType[static_cast<int>(EDataLayerType::Size)] = { TEXT("DataLayer.Runtime") , TEXT("DataLayer.Editor"), TEXT("") };

	constexpr const TCHAR* GetDataLayerIconName(EDataLayerType DataLayerType)
	{
		return IconNameByType[static_cast<uint32>(DataLayerType)];
	}
#endif

#if DATALAYER_TO_INSTANCE_RUNTIME_CONVERSION_ENABLED
	UE_DEPRECATED(5.1, "Label usage is deprecated.")
	static FName GetSanitizedDataLayerLabel(FName InDataLayerLabel)
	{
		return FName(InDataLayerLabel.ToString().TrimStartAndEnd().Replace(TEXT("\""), TEXT("")));
	}

#endif
};