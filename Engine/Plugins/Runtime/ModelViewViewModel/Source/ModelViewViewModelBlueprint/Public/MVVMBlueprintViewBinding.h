// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMExecutionMode.h"

#include "MVVMBlueprintViewBinding.generated.h"

class UWidgetBlueprint;
class UMVVMBlueprintView;

/**
*
*/
USTRUCT(BlueprintType)
struct MODELVIEWVIEWMODELBLUEPRINT_API FMVVMBlueprintViewConversionPath
{
	GENERATED_BODY()

	/** The Conversion function when converting the value from the destination to the source. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel", AdvancedDisplay)
	FMemberReference DestinationToSourceFunction;
	
	UPROPERTY()
	FName DestinationToSourceWrapper;

	/** The Conversion function when converting the value from the source to the destination. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel", AdvancedDisplay)
	FMemberReference SourceToDestinationFunction;

	UPROPERTY()
	FName SourceToDestinationWrapper;
};

/**
*
*/
USTRUCT(BlueprintType)
struct MODELVIEWVIEWMODELBLUEPRINT_API FMVVMBlueprintViewBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath SourcePath;

	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath DestinationPath;

	/** */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	EMVVMBindingMode BindingType = EMVVMBindingMode::OneWayToDestination;

	UPROPERTY()
	bool bOverrideExecutionMode = false;

	UPROPERTY(EditAnywhere, Category = "Viewmodel", meta=(EditCondition="bOverrideExecutionMode"))
	EMVVMExecutionMode OverrideExecutionMode = EMVVMExecutionMode::Immediate;

	/** */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	FMVVMBlueprintViewConversionPath Conversion;

	/** */
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel", Transient)
	TArray<FText> Errors;

	/** The unique ID of this binding. */
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FGuid BindingId;

	/** Whether the binding is enabled or disabled by default. The instance may enable the binding at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bEnabled = true;

	/** The binding is visible in the editor, but is not compiled and cannot be used at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bCompile = true;

	/**
	 * Get an internal name. For use in the UI, use GetDisplayNameString()
	 */
	FName GetFName() const;

	/** 
	 * Get a string that identifies this binding. 
	 * This is of the form: Widget.Property <- ViewModel.Property
	 */
	FString GetDisplayNameString(const UWidgetBlueprint* WidgetBlueprint) const;
};
