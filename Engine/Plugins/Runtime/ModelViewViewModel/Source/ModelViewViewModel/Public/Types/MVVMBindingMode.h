// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVMBindingMode.generated.h"


/** */
UENUM()
enum class EMVVMBindingMode : uint8
{
	OneTimeToDestination = 0,
	OneWayToDestination,
	TwoWay,
	OneTimeToSource UMETA(Hidden),
	OneWayToSource,
};


namespace UE::MVVM
{
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsForwardBinding(EMVVMBindingMode Mode);
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsBackwardBinding(EMVVMBindingMode Mode);
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsOneTimeBinding(EMVVMBindingMode Mode);
}