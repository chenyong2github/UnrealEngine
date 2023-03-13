// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IAnimNextParamInterface.generated.h"

namespace UE::AnimNext
{
struct FParam;
struct FContext;
}

UINTERFACE(BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class ANIMNEXTINTERFACE_API UAnimNextParamInterface : public UInterface
{
	GENERATED_BODY()
};

// Interface representing parameters that can be supplied to an anim interface
class ANIMNEXTINTERFACE_API IAnimNextParamInterface
{
	GENERATED_BODY()

	friend struct UE::AnimNext::FContext;

	// Get a parameter by name
	virtual bool GetParameter(FName InKey, UE::AnimNext::FParam& OutParam) = 0;

	// Get a parameter pointer by name
	virtual const UE::AnimNext::FParam* GetParameter(FName InKey) = 0;
};
