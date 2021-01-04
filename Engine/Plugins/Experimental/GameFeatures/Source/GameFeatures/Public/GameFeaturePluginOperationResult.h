// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/ValueOrError.h"

namespace UE
{
	namespace GameFeatures
	{
		struct FSuccessType {};

		using FResult = TValueOrError<FSuccessType, FString>;

		GAMEFEATURES_API FString ToString(const FResult& Result);
	}
}
