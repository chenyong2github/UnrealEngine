// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "UObject/GCObject.h"
#include "UObject/SoftObjectPtr.h"
#include "NiagaraClipboard.generated.h"

class UNiagaraDataInterface;
class UNiagaraScript;
class UNiagaraRendererProperties;

UENUM()
enum class ENiagaraClipboardFunctionInputValueMode
{
	Local,
	Linked,
	Data,
	Expression,
	Dynamic
};

class UNiagaraClipboardFunction;

UCLASS()
class NIAGARAEDITOR_API UNiagaraClipboardFunctionInput : public UObject
{
	GENERATED_BODY()

public:
	static const UNiagaraClipboardFunctionInput* CreateLocalValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TOptional<bool> bInEditConditionValue, TArray<uint8>& InLocalValueData);

	static const UNiagaraClipboardFunctionInput* CreateLinkedValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TOptional<bool> bInEditConditionValue, FName InLinkedValue);

	static const UNiagaraClipboardFunctionInput* CreateDataValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TOptional<bool> bInEditConditionValue, UNiagaraDataInterface* InDataValue);

	static const UNiagaraClipboardFunctionInput* CreateExpressionValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TOptional<bool> bInEditConditionValue, const FString& InExpressionValue);

	static const UNiagaraClipboardFunctionInput* CreateDynamicValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TOptional<bool> bInEditConditionValue, FString InDynamicValueName, UNiagaraScript* InDynamicValue);

	UPROPERTY()
	FName InputName;

	UPROPERTY()
	FNiagaraTypeDefinition InputType;

	UPROPERTY()
	bool bHasEditCondition;

	UPROPERTY()
	bool bEditConditionValue;

	UPROPERTY()
	ENiagaraClipboardFunctionInputValueMode ValueMode;

	UPROPERTY()
	TArray<uint8> Local;

	UPROPERTY()
	FName Linked;

	UPROPERTY()
	UNiagaraDataInterface* Data;

	UPROPERTY()
	FString Expression;

	UPROPERTY()
	UNiagaraClipboardFunction* Dynamic;

	bool CopyValuesFrom(const UNiagaraClipboardFunctionInput* InOther);
};

UENUM()
enum class ENiagaraClipboardFunctionScriptMode
{
	ScriptAsset,
	Assignment
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraClipboardFunction : public UObject
{
	GENERATED_BODY()

public:
	static UNiagaraClipboardFunction* CreateScriptFunction(UObject* InOuter, FString InFunctionName, UNiagaraScript* InScript);

	static UNiagaraClipboardFunction* CreateAssignmentFunction(UObject* InOuter, FString InFunctionName, const TArray<FNiagaraVariable>& InAssignmentTargets, const TArray<FString>& InAssignmentDefaults);

	UPROPERTY()
	FString FunctionName;

	UPROPERTY()
	FText DisplayName;

	UPROPERTY()
	ENiagaraClipboardFunctionScriptMode ScriptMode;

	UPROPERTY()
	TSoftObjectPtr<UNiagaraScript> Script;

	UPROPERTY()
	TArray<FNiagaraVariable> AssignmentTargets;

	UPROPERTY()
	TArray<FString> AssignmentDefaults;

	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> Inputs;
};

UCLASS()
class UNiagaraClipboardContent : public UObject
{
	GENERATED_BODY()

public:
	static UNiagaraClipboardContent* Create();

	UPROPERTY()
	TArray<const UNiagaraClipboardFunction*> Functions;

	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> FunctionInputs;

	UPROPERTY()
	TArray<const UNiagaraRendererProperties*> Renderers;

	UPROPERTY()
	TArray<const UNiagaraScript*> Scripts;
};

class FNiagaraClipboard
{
public:
	FNiagaraClipboard();

	void SetClipboardContent(UNiagaraClipboardContent* ClipboardContent);

	const UNiagaraClipboardContent* GetClipboardContent() const;
};

UCLASS()
class UNiagaraClipboardEditorScriptingUtilities : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Input")
	static void TryGetInputByName(const TArray<UNiagaraClipboardFunctionInput*>& InInputs, FName InInputName, bool& bOutSucceeded, UNiagaraClipboardFunctionInput*& OutInput);

	UFUNCTION(BlueprintPure, Category = "Input")
	static void TryGetLocalValueAsFloat(const UNiagaraClipboardFunctionInput* InInput, bool& bOutSucceeded, float& OutValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static void TryGetLocalValueAsInt(const UNiagaraClipboardFunctionInput* InInput, bool& bOutSucceeded, int32& OutValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static void TrySetLocalValueAsInt(UNiagaraClipboardFunctionInput* InInput, bool& bOutSucceeded, int32 InValue, bool bLooseTyping = true);

	UFUNCTION(BlueprintPure, Category = "Input")
	static FName GetTypeName(const UNiagaraClipboardFunctionInput* InInput);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateFloatLocalValueInput(UObject* InOuter, FName InInputName, bool bInHasEditCondition, bool bInEditConditionValue, float InLocalValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateIntLocalValueInput(UObject* InOuter, FName InInputName, bool bInHasEditCondition, bool bInEditConditionValue, int32 InLocalValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateLinkedValueInput(UObject* InOuter, FName InInputName, FName InInputTypeName, bool bInHasEditCondition, bool bInEditConditionValue, FName InLinkedValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateDataValueInput(UObject* InOuter, FName InInputName, FName InInputTypeName, bool bInHasEditCondition, bool bInEditConditionValue, UNiagaraDataInterface* InDataValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateExpressionValueInput(UObject* InOuter, FName InInputName, FName InInputTypeName, bool bInHasEditCondition, bool bInEditConditionValue, const FString& InExpressionValue);

	UFUNCTION(BlueprintPure, Category = "Input")
	static UNiagaraClipboardFunctionInput* CreateDynamicValueInput(UObject* InOuter, FName InInputName, FName InInputTypeName, bool bInHasEditCondition, bool bInEditConditionValue, FString InDynamicValueName, UNiagaraScript* InDynamicValue);
};