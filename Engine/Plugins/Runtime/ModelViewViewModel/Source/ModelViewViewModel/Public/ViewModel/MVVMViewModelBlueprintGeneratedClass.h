// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/BlueprintGeneratedClass.h"

#include "MVVMViewModelBlueprintGeneratedClass.generated.h"

namespace UE::FieldNotification { struct FFieldId; }
struct FFieldNotificationId;

class UMVVMViewModelBase;

namespace UE::MVVM
{
	class FViewModelBlueprintCompilerContext;
}//namespace

UCLASS(Deprecated)
class UE_DEPRECATED(5.3, "The prototype viewmodel editor is deprecated. Use the regular Blueprint editor.")
MODELVIEWVIEWMODEL_API UDEPRECATED_MVVMViewModelBlueprintGeneratedClass : public UBlueprintGeneratedClass
{
	GENERATED_BODY()
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "FieldNotification/FieldId.h"
#endif
