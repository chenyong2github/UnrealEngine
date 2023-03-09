// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Extensions/UserWidgetExtension.h"

#include "MVVMView.generated.h"

class INotifyFieldValueChanged;
namespace UE::FieldNotification { struct FFieldId; }
template <typename InterfaceType> class TScriptInterface;

class UMVVMViewClass;
struct FMVVMViewClass_CompiledBinding;
struct FMVVMViewDelayedBinding;
class UMVVMViewModelBase;
class UWidget;
namespace UE::MVVM
{
	class FDebugging;
}

USTRUCT()
struct FMVVMViewSource
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	TObjectPtr<UObject> Source; // TScriptInterface<INotifyFieldValueChanged>

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FName SourceName;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	int32 RegisteredCout = 0;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	bool bCreatedSource = false;
};

/**
 * Instance UMVVMClassExtension_View for the UUserWdiget
 */
UCLASS(Transient, DisplayName="MVVM View")
class MODELVIEWVIEWMODEL_API UMVVMView : public UUserWidgetExtension
{
	GENERATED_BODY()

	friend UE::MVVM::FDebugging;

public:
	void ConstructView(const UMVVMViewClass* ClassExtension);

	//~ Begin UUserWidgetExtension implementation
	virtual void Construct() override;
	virtual void Destruct() override;
	//~ End UUserWidgetExtension implementation

	const UMVVMViewClass* GetViewClass() const
	{
		return ClassExtension;
	}

	void ExecuteDelayedBinding(const FMVVMViewDelayedBinding& DelayedBinding) const;
	void ExecuteEveryTickBindings() const;

// todo a way to identify a binding from outside. maybe a unique name in the editor?
	//UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	//void SetLibraryBindingEnabled(FGuid ViewModelId, FMVVMBindingName BindingName, bool bEnable);

	//UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	//bool IsLibraryBindingEnabled(FGuid ViewModelId, FMVVMBindingName BindingName) const;

	UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	TScriptInterface<INotifyFieldValueChanged> GetViewModel(FName ViewModelName) const;

	UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	bool SetViewModel(FName ViewModelName, TScriptInterface<INotifyFieldValueChanged> ViewModel);

private:
	void HandledLibraryBindingValueChanged(UObject* InViewModel, UE::FieldNotification::FFieldId InFieldId, int32 InCompiledBindingIndex) const;

	void ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding) const;
	void ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, UObject* Source) const;

	void EnableLibraryBinding(const FMVVMViewClass_CompiledBinding& Item, int32 BindingIndex);
	void DisableLibraryBinding(const FMVVMViewClass_CompiledBinding& Item, int32 BindingIndex);
	bool IsLibraryBindingEnabled(int32 InBindindIndex) const;

	bool RegisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex);
	void UnregisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding);

	TScriptInterface<INotifyFieldValueChanged> FindSource(const FMVVMViewClass_CompiledBinding& Binding, bool bAllowNull) const;
	FMVVMViewSource* FindViewSource(const FName SourceName);
	const FMVVMViewSource* FindViewSource(const FName SourceName) const;

private:
// todo support dynamic runtime binding.
	/** Binding that are added dynamically at runtime. */
	//TArray<FMVVMView_Binding> RegisteredDynamicBindings;

	UPROPERTY(Transient)
	TObjectPtr<const UMVVMViewClass> ClassExtension;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	TArray<FMVVMViewSource> Sources;

	/** The binding that are enabled for the instance. */
	TBitArray<> EnabledLibraryBindings;

	/** Should log when a binding is executed. */
	UPROPERTY(EditAnywhere, Transient, Category = "Viewmodel")
	bool bLogBinding = false;

	/** Is the Construct method was called. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	bool bConstructed = false;

	/** The view has at least one binding that need to be ticked every frame. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	bool bHasEveryTickBinding = false;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "Templates/SubclassOf.h"
#include "Types/MVVMBindingName.h"
#endif
