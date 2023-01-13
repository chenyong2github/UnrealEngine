// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"


#include "UObject/Package.h"
#include "MVVMEditorSubsystem.generated.h"

class UEdGraphPin;
class UMVVMBlueprintView;
enum class EMVVMBindingMode : uint8;
enum class EMVVMExecutionMode : uint8;
namespace UE::MVVM { struct FBindingSource; }
struct FMVVMAvailableBinding;
struct FMVVMBlueprintPropertyPath;
struct FMVVMBlueprintViewBinding;
template <typename T> class TSubclassOf;

class UEdGraph;
class UK2Node_CallFunction;
class UWidgetBlueprint;

/** */
UCLASS()
class MODELVIEWVIEWMODELEDITOR_API UMVVMEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="MVVM")
	UMVVMBlueprintView* RequestView(UWidgetBlueprint* WidgetBlueprint) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	UMVVMBlueprintView* GetView(const UWidgetBlueprint* WidgetBlueprint) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	FName AddViewModel(UWidgetBlueprint* WidgetBlueprint, const UClass* ViewModel);
	
	UFUNCTION(BlueprintCallable, Category = "MVVM")
	void RemoveViewModel(UWidgetBlueprint* WidgetBlueprint, FName ViewModel);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool VerifyViewModelRename(UWidgetBlueprint* WidgetBlueprint, FName ViewModel, FName NewViewModel, FText& OutError);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool RenameViewModel(UWidgetBlueprint* WidgetBlueprint, FName ViewModel, FName NewViewModel, FText& OutError);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	FMVVMBlueprintViewBinding& AddBinding(UWidgetBlueprint* WidgetBlueprint);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	void RemoveBinding(UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	TArray<FMVVMAvailableBinding> GetChildViewModels(TSubclassOf<UObject> Class, TSubclassOf<UObject> Accessor);

	void SetSourceToDestinationConversionFunction(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, const UFunction* ConversionFunction);
	void SetDestinationToSourceConversionFunction(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, const UFunction* ConversionFunction);
	void SetWidgetPropertyForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, FMVVMBlueprintPropertyPath Field);
	void SetViewModelPropertyForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, FMVVMBlueprintPropertyPath Field);
	void OverrideExecutionModeForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, EMVVMExecutionMode Mode);
	void ResetExecutionModeForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding);
	void SetBindingTypeForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, EMVVMBindingMode Type);
	void SetEnabledForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, bool bEnabled);
	void SetCompileForBinding(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, bool bCompile);

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool IsValidConversionFunction(const UFunction* Function, const FMVVMBlueprintPropertyPath& Source, const FMVVMBlueprintPropertyPath& Destination) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool IsSimpleConversionFunction(const UFunction* Function) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	UEdGraph* GetConversionFunctionGraph(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	UFunction* GetConversionFunction(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	UK2Node_CallFunction* GetConversionFunctionNode(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	TArray<UFunction*> GetAvailableConversionFunctions(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintPropertyPath& Source, const FMVVMBlueprintPropertyPath& Destination) const;

	FMVVMBlueprintPropertyPath GetPathForConversionFunctionArgument(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, FName ArgumentName, bool bSourceToDestination) const;
	void SetPathForConversionFunctionArgument(UWidgetBlueprint* WidgetBlueprint, FMVVMBlueprintViewBinding& Binding, FName ArgumentName, const FMVVMBlueprintPropertyPath& Path, bool bSourceToDestination) const;
	UEdGraphPin* GetConversionFunctionArgumentPin(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, FName ParameterName, bool bSourceToDestination);

	TArray<UE::MVVM::FBindingSource> GetBindableWidgets(const UWidgetBlueprint* WidgetBlueprint) const;
	
	TArray<UE::MVVM::FBindingSource> GetAllViewModels(const UWidgetBlueprint* WidgetBlueprint) const;

private:
	FName GetConversionFunctionWrapperName(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination) const;
	UEdGraph* CreateConversionFunctionWrapperGraph(UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, const UFunction* ConversionFunction, bool bSourceToDestination);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "MVVMBlueprintView.h"
#include "Templates/SubclassOf.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMExecutionMode.h"
#include "Types/MVVMFieldVariant.h"
#endif
