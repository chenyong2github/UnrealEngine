// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "UObject/GCObject.h"
#include "NiagaraClipboard.generated.h"

class UNiagaraDataInterface;
class UNiagaraScript;

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
	static const UNiagaraClipboardFunctionInput* CreateLocalValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TArray<uint8>& InLocalValueData);

	static const UNiagaraClipboardFunctionInput* CreateLinkedValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, FName InLinkedValue);

	static const UNiagaraClipboardFunctionInput* CreateDataValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, UNiagaraDataInterface* InDataValue);

	static const UNiagaraClipboardFunctionInput* CreateExpressionValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, const FString& InExpressionValue);

	static const UNiagaraClipboardFunctionInput* CreateDynamicValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, UNiagaraScript* InDynamicValue);

	UPROPERTY()
	FName InputName;

	UPROPERTY()
	FNiagaraTypeDefinition InputType;

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
	static UNiagaraClipboardFunction* CreateScriptFunction(UObject* InOuter, UNiagaraScript* InScript);

	UPROPERTY()
	ENiagaraClipboardFunctionScriptMode ScriptMode;

	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY()
	TArray<FNiagaraVariable> AssignmentTargets;

	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> Inputs;
};

UCLASS()
class UNiagaraClipboardContent : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<const UNiagaraClipboardFunction*> Functions;

	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> FunctionInputs;
};

class FNiagaraClipboard
{
public:
	FNiagaraClipboard();

	void Copy(const UNiagaraClipboardFunctionInput* Input);

	const UNiagaraClipboardFunctionInput* GetCopiedInput() const;

private:
	void SetClipboardContentInternal(UNiagaraClipboardContent* ClipboardContent);
	const UNiagaraClipboardContent* GetClipboardContentInternal() const;
};