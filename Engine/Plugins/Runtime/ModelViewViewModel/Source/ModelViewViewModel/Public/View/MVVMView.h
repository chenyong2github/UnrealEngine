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

	// Number of bindings connected to the source.
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	int32 RegisteredCount = 0;

	// The source is defined in the ClassExtension.
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	bool bCreatedSource = false;

	// The source was set manually via SetViewModel.
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	bool bSetManually = false;

	// The source was set to a UserWidget property.
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	bool bAssignedToUserWidgetProperty = false;
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
	//virtual void Initialize() override;
	virtual void Construct() override;
	virtual void Destruct() override;
	//~ End UUserWidgetExtension implementation

	void InitializeBindings();
	void DeintializeBindings();

	const UMVVMViewClass* GetViewClass() const
	{
		return ClassExtension;
	}

	void ExecuteDelayedBinding(const FMVVMViewDelayedBinding& DelayedBinding) const;
	void ExecuteEveryTickBindings() const;

	/** Find and return the viewmodel with the specified name. */
	UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	TScriptInterface<INotifyFieldValueChanged> GetViewModel(FName ViewModelName) const;

	/**
	 * Set the viewmodel of the specified name.
	 * The viewmodel needs to be settable and the type should match (child of the defined viewmodel).
	 * If the view is initialized, all bindings that uses that viewmodel will be re-executed with the new viewmodel instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	bool SetViewModel(FName ViewModelName, TScriptInterface<INotifyFieldValueChanged> ViewModel);

private:
	bool EvaluateSourceCreator(int32 SourceIndex);
	bool SetSourceInternal(FName ViewModelName, TScriptInterface<INotifyFieldValueChanged> ViewModel, bool bForDynamicSource);

	void HandledLibraryBindingValueChanged(UObject* InViewModel, UE::FieldNotification::FFieldId InFieldId, int32 InCompiledBindingIndex) const;

	void ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex) const;

	void EnableLibraryBinding(const FMVVMViewClass_CompiledBinding& Item, int32 BindingIndex);
	void DisableLibraryBinding(const FMVVMViewClass_CompiledBinding& Item, int32 BindingIndex);
	bool IsLibraryBindingEnabled(int32 InBindindIndex) const;

	bool RegisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex);
	void UnregisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex);

	TScriptInterface<INotifyFieldValueChanged> FindSource(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex, bool bAllowNull) const;
	FMVVMViewSource* FindViewSource(const FName SourceName);
	const FMVVMViewSource* FindViewSource(const FName SourceName) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<const UMVVMViewClass> ClassExtension;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	TArray<FMVVMViewSource> Sources;

	/** The binding that are registered by the view to the sources. */
	TBitArray<> RegisteredLibraryBindings;

	/** Should log when a binding is executed. */
	UPROPERTY(EditAnywhere, Transient, Category = "Viewmodel")
	bool bLogBinding = false;

	/** Is the Construct method called. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	bool bConstructed = false;
	
	/** Is the Initialize method called. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Viewmodel")
	bool bInitialized = false;

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
