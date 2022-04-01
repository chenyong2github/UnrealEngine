// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"

#include "MVVMBlueprintViewBinding.generated.h"

/**
*
*/
USTRUCT(BlueprintType)
struct FMVVMBlueprintViewConversionPath
{
	GENERATED_BODY()

	/** The Conversion function when setting the binding. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay)
	FString SetConversionFunctionPath;

	/** The Conversion function when getting the binding (in 2 way). */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay)
	FString GetConversionFunctionPath;
};

/**
*
*/
USTRUCT(BlueprintType)
struct FMVVMBlueprintViewBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMViewModelPropertyPath ViewModelPath;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMWidgetPropertyPath WidgetPath;

	/** */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	EMVVMBindingMode BindingType = EMVVMBindingMode::OneWayToDestination;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	EMVVMViewBindingUpdateMode UpdateMode = EMVVMViewBindingUpdateMode::Immediate;

	/** */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMBlueprintViewConversionPath Conversion;

	/** */
	UPROPERTY(VisibleAnywhere, Category = "MVVM", Transient)
	TArray<FText> Errors;

	/** Whether the binding is enabled or disabled by default. The instance may enable the binding at runtime. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	bool bEnabled = true;

	/** The binding is visible in the editor, but is not compiled and cannot be used at runtime. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	bool bCompile = true;
};
