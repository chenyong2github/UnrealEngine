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
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<TArray<FMVVMConstFieldVariant>, FString> GenerateFieldPathList(TSubclassOf<UObject> From, FStringView FieldPath, bool bForSourceBinding);

	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<TArray<FMVVMConstFieldVariant>, FString> GenerateFieldPathList(const TArrayView<FMVVMConstFieldVariant>, bool bForSourceBinding);

	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API FString ToString(const TArrayView<FMVVMFieldVariant> Fields);
	UE_NODISCARD MODELVIEWVIEWMODEL_API FString ToString(const TArrayView<FMVVMConstFieldVariant> Fields);

	/** */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<UObject*, void> EvaluateObjectProperty(const FFieldContext& InSource);

} // namespace
