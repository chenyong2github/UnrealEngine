// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "MVVMBlueprintView.h"
#include "Templates/SubclassOf.h"
#include "Types/MVVMFieldVariant.h"

#include "MVVMEditorSubsystem.generated.h"

class UMVVMViewModelBase;
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
	UMVVMBlueprintView* GetView(UWidgetBlueprint* WidgetBlueprint) const;
	
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

	TArray<UE::MVVM::FMVVMConstFieldVariant> GetChildViewModels(UClass* Class);

	TArray<const UFunction*> GetAvailableConversionFunctions(const UE::MVVM::FMVVMConstFieldVariant& Source, const UE::MVVM::FMVVMConstFieldVariant& Dest) const;
};
