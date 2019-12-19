// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "UObject/GCObject.h"
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
	ENiagaraClipboardFunctionScriptMode ScriptMode;

	UPROPERTY()
	UNiagaraScript* Script;

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
};

class FNiagaraClipboard
{
public:
	FNiagaraClipboard();

	void SetClipboardContent(UNiagaraClipboardContent* ClipboardContent);

	const UNiagaraClipboardContent* GetClipboardContent() const;
};