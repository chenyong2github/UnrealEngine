// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/ScriptMacros.h"
#include "CommonPoolableWidgetInterface.generated.h"

UINTERFACE()
class COMMONUI_API UCommonPoolableWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

class COMMONUI_API ICommonPoolableWidgetInterface
{
	GENERATED_BODY()

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "Common Poolable Widget")
	void OnAcquireFromPool();

	UFUNCTION(BlueprintNativeEvent, Category = "Common Poolable Widget")
	void OnReleaseToPool();
};