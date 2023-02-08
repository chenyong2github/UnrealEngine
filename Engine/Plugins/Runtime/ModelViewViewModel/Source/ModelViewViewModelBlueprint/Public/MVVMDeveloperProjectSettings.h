// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Types/MVVMExecutionMode.h"

#include "MVVMDeveloperProjectSettings.generated.h"

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

	bool IsExecutionModeAllowed(EMVVMExecutionMode ExecutionMode) const
	{
		return AllowedExecutionMode.Contains(ExecutionMode);
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

public:
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
