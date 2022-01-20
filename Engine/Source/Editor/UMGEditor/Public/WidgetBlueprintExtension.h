// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Blueprint/BlueprintExtension.h"
#include "WidgetBlueprint.h"

class FSubobjectCollection;
class FWidgetBlueprintCompilerContext;
class UWidgetBlueprintGeneratedClass;

#include "WidgetBlueprintExtension.generated.h"

/** Extension that allows per-system data to be held on the widget blueprint, and per-system logic to be executed during compilation */
UCLASS()
class UMGEDITOR_API UWidgetBlueprintExtension : public UBlueprintExtension
{
	GENERATED_BODY()

public:
	/**
	 * Request an WidgetBlueprintExtension for an WidgetBlueprint.
	 * @note It is illegal to perform this operation once compilation has commenced, use GetExtension instead.
	 */
	template<typename ExtensionType>
	static ExtensionType* RequestExtension(UWidgetBlueprint* InWidgetBlueprint)
	{
		return CastChecked<ExtensionType>(RequestExtension(InWidgetBlueprint, ExtensionType::StaticClass()));
	}

	/**
	 * Request an WidgetBlueprintExtension for an WidgetBlueprint.
	 * @note It is illegal to perform this operation once compilation has commenced, use GetExtension instead.
	 */
	static UWidgetBlueprintExtension* RequestExtension(UWidgetBlueprint* InWidgetBlueprint, TSubclassOf<UWidgetBlueprintExtension> InExtensionType);

	/** Get an already-requested extension for an WidgetBlueprint. */
	template<typename ExtensionType>
    static ExtensionType* GetExtension(const UWidgetBlueprint* InWidgetBlueprint)
	{
		return Cast<ExtensionType>(GetExtension(InWidgetBlueprint, ExtensionType::StaticClass()));
	}

	/** Get an already-requested extension for an WidgetBlueprint. */
	static UWidgetBlueprintExtension* GetExtension(const UWidgetBlueprint* InWidgetBlueprint, TSubclassOf<UWidgetBlueprintExtension> InExtensionType);

	/** Get all subsystems currently present on an WidgetBlueprint */
	static TArray<UWidgetBlueprintExtension*> GetExtensions(const UWidgetBlueprint* InWidgetBlueprint);

	/** Iterate over all registered WidgetBlueprintExtensions in an WidgetBlueprint */
	template<typename Predicate>
	static void ForEachExtension(const UWidgetBlueprint* InWidgetBlueprint, Predicate Pred)
	{
		for (UBlueprintExtension* BlueprintExtension : InWidgetBlueprint->Extensions)
		{
			if (UWidgetBlueprintExtension* WidgetBlueprintExtension = Cast<UWidgetBlueprintExtension>(BlueprintExtension))
			{
				Pred(WidgetBlueprintExtension);
			}
		}
	}

	/** Get the WidgetBlueprint that hosts this extension */
	UWidgetBlueprint* GetWidgetBlueprint() const;

protected:
	/** 
	 * Override point called when a compiler context is created for the WidgetBlueprint
	 * @param	InCreationContext	The compiler context for the current compilation
	 */
	virtual void HandleBeginCompilation(FWidgetBlueprintCompilerContext& InCreationContext) {}

	virtual void HandleCreateFunctionList() {}
	virtual void HandleCleanAndSanitizeClass(UWidgetBlueprintGeneratedClass* ClassToClean, UObject* OldCDO) {}
	virtual void HandleCreateClassVariablesFromBlueprint() {}
	virtual void HandleCopyTermDefaultsToDefaultObject(UObject* DefaultObject) {}
	virtual void HandleFinishCompilingClass(UWidgetBlueprintGeneratedClass* Class) {}
	virtual bool HandleValidateGeneratedClass(UWidgetBlueprintGeneratedClass* Class) { return true; }

	/**
	 * Override point called when a compiler context is destroyed for the WidgetBlueprint.
	 * Can be used to clean up resources.
	 */
	virtual void HandleEndCompilation() {}


private:
	friend FWidgetBlueprintCompilerContext;

	void BeginCompilation(FWidgetBlueprintCompilerContext& InCreationContext)
	{
		HandleBeginCompilation(InCreationContext);
	}

	void CreateFunctionList()
	{
		HandleCreateFunctionList();
	}

	void CleanAndSanitizeClass(UWidgetBlueprintGeneratedClass* ClassToClean, UObject* OldCDO)
	{
		HandleCleanAndSanitizeClass(ClassToClean, OldCDO);
	}

	void CreateClassVariablesFromBlueprint()
	{
		HandleCreateClassVariablesFromBlueprint();
	}

	void CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
	{
		HandleCopyTermDefaultsToDefaultObject(DefaultObject);
	}

	void FinishCompilingClass(UWidgetBlueprintGeneratedClass* Class)
	{
		HandleFinishCompilingClass(Class);
	}

	bool ValidateGeneratedClass(UWidgetBlueprintGeneratedClass* Class)
	{
		return HandleValidateGeneratedClass(Class);
	}

	void EndCompilation()
	{
		HandleEndCompilation();
	}
};
