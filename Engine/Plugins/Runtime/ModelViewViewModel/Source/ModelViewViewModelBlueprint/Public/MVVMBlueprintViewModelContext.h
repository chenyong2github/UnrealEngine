// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MVVMViewModelBase.h"

#include "MVVMBlueprintViewModelContext.generated.h"

/**
 *
 */
UENUM()
enum class EMVVMBlueprintViewModelContextCreationType : uint8
{
	Manual,	// The viewmodel will be assigned later.
	CreateInstance, // A new instance of the viewmodel will be created when the widget is created.
	GlobalViewModelCollection, // The viewmodel exists and is added to the MVVMSubsystem. It will be fetched there.
	PropertyPath, // The viewmodel will be fetched by evaluating a function or a property path.
};

/**
 *
 */
USTRUCT(BlueprintType)
struct MODELVIEWVIEWMODELBLUEPRINT_API FMVVMBlueprintViewModelContext
{
	GENERATED_BODY()

public:
	FMVVMBlueprintViewModelContext() {}
	FMVVMBlueprintViewModelContext(TSubclassOf<UMVVMViewModelBase> InClass, FGuid InGuid);

	FGuid GetViewModelId() const
	{
		return ViewModelContextId;
	}

	FName GetViewModelName() const
	{
		//todo change that to the fname
		return *GetDisplayName().ToString();
	}

	FText GetDisplayName() const;

	TSubclassOf<UMVVMViewModelBase> GetViewModelClass() const
	{
		return ViewModelClass;
	}

private:
	/** When the view is spawn, create an instance of the viewmodel. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FGuid ViewModelContextId;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	TSubclassOf<UMVVMViewModelBase> ViewModelClass;

public:
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FText OverrideDisplayName;

	/** When the view is spawn, create an instance of the viewmodel. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	EMVVMBlueprintViewModelContextCreationType CreationType = EMVVMBlueprintViewModelContextCreationType::CreateInstance;

	/** Identifier of an already registered viewmodel. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay, meta = (EditCondition = "CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection"))
	FName GlobalViewModelIdentifier;

	/** The Path to get the viewmodel instance. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay, meta = (EditCondition = "CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath"))
	FString ViewModelPropertyPath;

	/** Generate a setter function for this viewmodel. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay)
	bool bCreateSetterFunction = false;
};
