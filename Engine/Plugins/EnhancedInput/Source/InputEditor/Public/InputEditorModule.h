// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "TickableEditorObject.h"

#include "InputEditorModule.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEnhancedInputEditor, Log, All);

////////////////////////////////////////////////////////////////////
// FInputEditorModule

class UInputAction;
class SWindow;
class UPlayerMappableKeySettings;

class FInputEditorModule : public IModuleInterface, public FTickableEditorObject
{
public:

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface interface

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FInputEditorModule, STATGROUP_Tickables); }
	// End FTickableEditorObject interface

	static EAssetTypeCategories::Type GetInputAssetsCategory() { return InputAssetsCategory; }
	
	/** Returns a pointer to the player mappable settings object that has this mapping name */
	INPUTEDITOR_API static const UPlayerMappableKeySettings* FindMappingByName(const FName InName);

	/** Returns true if the given name is in use by a player mappable key setting */
	INPUTEDITOR_API static bool IsMappingNameInUse(const FName InName);
		
private:
	void RegisterAssetTypeActions(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	}

	void OnMainFrameCreationFinished(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog);
	
	/** Automatically upgrade the current project to use Enhanced Input if it is currently set to the legacy input classes. */
	void AutoUpgradeDefaultInputClasses();

	static EAssetTypeCategories::Type InputAssetsCategory;
	
	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;
	
	TSharedPtr<class FSlateStyleSet> StyleSet;
};

////////////////////////////////////////////////////////////////////
// Asset factories

UCLASS()
class INPUTEDITOR_API UInputMappingContext_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, Category=InputMappingContext)
	TSubclassOf<class UInputMappingContext> InputMappingContextClass;

	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	/** Set the array of initial actions that the resulting IMC should be populated with */
	void SetInitialActions(TArray<TWeakObjectPtr<UInputAction>> InInitialActions);
	
protected:

	/** An array of Input Actions that the mapping context should be populated with upon creation */
	TArray<TWeakObjectPtr<UInputAction>> InitialActions;
	
};

UCLASS()
class INPUTEDITOR_API UInputAction_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, Category=InputAction)
	TSubclassOf<UInputAction> InputActionClass;

	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UE_DEPRECATED(5.3, "UPlayerMappableInputConfig has been deprecated, please use the UEnhancedInputUserSettings system instead.") INPUTEDITOR_API UPlayerMappableInputConfig_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "IDetailsView.h"
#endif
