// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "MVVMDeveloperProjectSettings.generated.h"

enum class EMVVMBlueprintViewModelContextCreationType : uint8;
enum class EMVVMExecutionMode : uint8;

/**
 * 
 */
USTRUCT()
struct FMVVMDeveloperProjectWidgetSettings
{
	GENERATED_BODY()

	/** Properties or functions name that should not be use for binding (read or write). */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	TSet<FName> DisallowedFieldNames;
	
	/** Properties or functions name that are displayed in the advanced category. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	TSet<FName> AdvancedFieldNames;
};

/**
 *
 */
UENUM()
enum class EMVVMDeveloperConversionFunctionFilterType : uint8
{
	BlueprintActionRegistry,
	AllowedList,
};


/**
 * Implements the settings for the MVVM Editor
 */
UCLASS(config=ModelViewViewModel, defaultconfig)
class MODELVIEWVIEWMODELBLUEPRINT_API UMVVMDeveloperProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMVVMDeveloperProjectSettings();

	virtual FName GetCategoryName() const override;
	virtual FText GetSectionText() const override;

	bool IsPropertyAllowed(const FProperty* Property) const;
	bool IsFunctionAllowed(const UFunction* Function) const;
	bool IsConversionFunctionAllowed(const UFunction* Function) const;

	bool IsExecutionModeAllowed(EMVVMExecutionMode ExecutionMode) const
	{
		return AllowedExecutionMode.Contains(ExecutionMode);
	}

	bool IsContextCreationTypeAllowed(EMVVMBlueprintViewModelContextCreationType ContextCreationType) const
	{
		return AllowedContextCreationType.Contains(ContextCreationType);
	}

	EMVVMDeveloperConversionFunctionFilterType GetConversionFunctionFilter() const
	{
		return ConversionFunctionFilter;
	}

	TArray<const UClass*> GetAllowedConversionFunctionClasses() const;

private:
	/** Permission list for filtering which properties are visible in UI. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	TMap<FSoftClassPath, FMVVMDeveloperProjectWidgetSettings> FieldSelectorPermissions;

	/** Permission list for filtering which execution mode is allowed. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	TSet<EMVVMExecutionMode> AllowedExecutionMode;
	
	/** Permission list for filtering which context creation type is allowed. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	TSet<EMVVMBlueprintViewModelContextCreationType> AllowedContextCreationType;

public:
	/** Binding can be made from the DetailView Bind option. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	bool bAllowBindingFromDetailView = true;

	/** When generating a source from the Viewmodel editor, allow the compiler to generate a setter function. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	bool bAllowGeneratedViewModelSetter = true;
	
	/** For the binding list widget, allow the user to edit the binding in the detail view. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	bool bShowDetailViewOptionInBindingPanel = true;

	/** Permission list for filtering which execution mode is allowed. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel")
	EMVVMDeveloperConversionFunctionFilterType ConversionFunctionFilter = EMVVMDeveloperConversionFunctionFilterType::BlueprintActionRegistry;

	/** Individual class that are allowed to be uses as conversion functions. */
	UPROPERTY(EditAnywhere, config, Category = "Viewmodel", meta = (EditCondition = "ConversionFunctionFilter == EMVVMDeveloperConversionFunctionFilterType::AllowedList"))
	TSet<FSoftClassPath> AllowedClassForConversionFunctions;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Components/Widget.h"
#include "CoreMinimal.h"
#endif
