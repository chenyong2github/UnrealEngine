// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "UObject/Field.h"
#include "Misc/Attribute.h"
#include "Features/IModularFeature.h"
#include "UObject/UnrealType.h"

class UBlueprint;
class IPropertyHandle;
class UEdGraph;
class FExtender;
class SWidget;
struct FSlateBrush;
struct FEdGraphPinType;

/** An element in a binding chain */
struct FBindingChainElement
{
	FBindingChainElement(FProperty* InProperty, int32 InArrayIndex = INDEX_NONE)
		: Field(InProperty)
		, ArrayIndex(InArrayIndex)
	{}

	FBindingChainElement(UFunction* InFunction)
		: Field(InFunction)
		, ArrayIndex(INDEX_NONE)
	{}

	/** Field that this this chain element refers to */
	FFieldVariant Field;

	/** Optional array index if this element refers to an array */
	int32 ArrayIndex = INDEX_NONE;
};

/** 
 * Info about a redirector binding. 
 * Redirector bindings allow 
 */
struct FRedirectorBindingInfo
{
	FRedirectorBindingInfo(FName InName, const FText& InDescription, UStruct* InStruct)
		: Name(InName)
		, Description(InDescription)
		, Struct(InStruct)
	{}

	/** The name of the binding */
	FName Name = NAME_None;

	/** Description of the binding, used as tooltip text */
	FText Description;

	/** The struct that the binding will output */
	UStruct* Struct = nullptr;
};

/** Delegate used to generate a new binding function's name */
DECLARE_DELEGATE_RetVal(FString, FOnGenerateBindingName);

/** Delegate used to open a binding (e.g. a function) */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnGotoBinding, FName /*InPropertyName*/);

/** Delegate used to se if we can open a binding (e.g. a function) */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanGotoBinding, FName /*InPropertyName*/);

/** Delegate used to check whether a property can be bound to the property in question */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanBindProperty, FProperty* /*InProperty*/);

/** Delegate used to check whether a function can be bound to the property in question */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanBindFunction, UFunction* /*InFunction*/);

/** Delegate called to see if a class can be bound to */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanBindToClass, UClass* /*InClass*/);

/** Delegate called to see if a subobject can be bound to */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanBindToSubObjectClass, UClass* /*InSubObjectClass*/);

/** Delegate called to add a binding */
DECLARE_DELEGATE_TwoParams(FOnAddBinding, FName /*InPropertyName*/, const TArray<FBindingChainElement>& /*InBindingChain*/);

/** Delegate called to remove a binding */
DECLARE_DELEGATE_OneParam(FOnRemoveBinding, FName /*InPropertyName*/);

/** Delegate called to see if we can remove remove a binding (ie. if it exists) */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanRemoveBinding, FName /*InPropertyName*/);

/** Setup arguments structure for a property binding widget */
struct FPropertyBindingWidgetArgs
{
	/** An optional bindable property */
	FProperty* Property = nullptr;

	/** An optional signature to use to match binding functions */
	UFunction* BindableSignature = nullptr;

	/** Delegate used to generate a new binding function's name */
	FOnGenerateBindingName OnGenerateBindingName;

	/** Delegate used to open a bound generated function */
	FOnGotoBinding OnGotoBinding;

	/** Delegate used to see if we can open a binding (e.g. a function) */
	FOnCanGotoBinding OnCanGotoBinding;

	/** Delegate used to check whether a property can be bound to the property in question */
	FOnCanBindProperty OnCanBindProperty;

	/** Delegate used to check whether a function can be bound to the property in question */
	FOnCanBindFunction OnCanBindFunction;

	/** Delegate called to see if a class can be bound to */
	FOnCanBindToClass OnCanBindToClass;

	/** Delegate called to see if a subobject can be bound to */
	FOnCanBindToSubObjectClass OnCanBindToSubObjectClass;

	/** Delegate called to add a binding */
	FOnAddBinding OnAddBinding;

	/** Delegate called to remove a binding */
	FOnRemoveBinding OnRemoveBinding;

	/** Delegate called to see if we can remove remove a binding (ie. if it exists) */
	FOnCanRemoveBinding OnCanRemoveBinding;

	/** The current binding's text label */
	TAttribute<FText> CurrentBindingText;

	/** The current binding's image */
	TAttribute<const FSlateBrush*> CurrentBindingImage;

	/** The current binding's color */
	TAttribute<FLinearColor> CurrentBindingColor;

	/** Menu extender */
	TSharedPtr<FExtender> MenuExtender;

	/** Whether to generate pure bindings */
	bool bGeneratePureBindings = true;

	/** Whether to allow array element bindings */
	bool bAllowArrayElementBindings = false;

	/** Whether to allow new bindings to be made from within the widget's UI */
	bool bAllowNewBindings = true;

	/** Whether to allow UObject functions as non-leaf nodes */
	bool bAllowUObjectFunctions = false;
};

/** Enum describing the result of ResolveLeafProperty */
enum class EPropertyAccessResolveResult
{
	/** Resolution of the path failed */
	Failed,

	/** Resolution of the path failed and the property is internal to the initial context */
	SucceededInternal,

	/** Resolution of the path failed and the property is external to the initial context (i.e. uses an object/redirector indirection) */
	SucceededExternal,
};

/** Enum describing property compatibility */
enum class EPropertyAccessCompatibility
{
	// Properties are incompatible
	Incompatible,

	// Properties are directly compatible
	Compatible,	

	// Properties can be copied with a simple type promotion
	Promotable,
};

/** Editor support for property access system */
class IPropertyAccessEditor : public IModularFeature
{
public:
	virtual ~IPropertyAccessEditor() {}

	/** 
	 * Make a property binding widget.
	 * @param	InBlueprint		The blueprint that the binding will exist within
	 * @param	InArgs			Optional arguments for the widget
	 * @return a new binding widget
	 */
	virtual TSharedRef<SWidget> MakePropertyBindingWidget(UBlueprint* InBlueprint, const FPropertyBindingWidgetArgs& InArgs = FPropertyBindingWidgetArgs()) const = 0;

	/** Resolve a property path to a structure, returning the leaf property and array index if any. @return true if resolution succeeded */
	virtual EPropertyAccessResolveResult ResolveLeafProperty(const UStruct* InStruct, TArrayView<FString> InPath, FProperty*& OutProperty, int32& OutArrayIndex) const = 0;

	// Get the compatibility of the two supplied properties. Ordering matters for promotion (A->B).
	virtual EPropertyAccessCompatibility GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB) const = 0;

	// Get the compatibility of the two supplied pin types. Ordering matters for promotion (A->B).
	virtual EPropertyAccessCompatibility GetPinTypeCompatibility(const FEdGraphPinType& InPinTypeA, const FEdGraphPinType& InPinTypeB) const = 0;

	// Makes a string path from a binding chain
	virtual void MakeStringPath(const TArray<FBindingChainElement>& InBindingChain, TArray<FString>& OutStringPath) const = 0;
};