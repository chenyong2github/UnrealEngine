// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMFieldContext.h"
#include "UObject/FieldPath.h"
#include "UObject/Object.h"


namespace UE::MVVM::FieldPathHelper
{
	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<TArray<FMVVMFieldVariant>, FString> GenerateFieldPathList(TSubclassOf<UObject> From, FStringView FieldPath, bool bForSourceBinding);

	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<TArray<FMVVMFieldVariant>, FString> GenerateConversionFunctionFieldPathList(TSubclassOf<UObject> From, FStringView FieldPath);

	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<UObject*, void> EvaluateObjectProperty(const FFieldContext& InSource);

} // namespace
