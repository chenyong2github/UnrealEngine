// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NiagaraParameterMapHistory.h"
#include "UObject/Object.h"
#include "NiagaraTypes.h"
#include "UpgradeNiagaraScriptResults.generated.h"

class UNiagaraClipboardFunctionInput;

UENUM()
enum class ENiagaraPythonScriptInputSource : uint32
{
	Input,

    Output,

    Local,

    InputOutput,

    InitialValueInput,

    // insert new script parameter usages before
    None UMETA(Hidden),
	
    Num UMETA(Hidden)
};

/** Wrapper for setting the value on a parameter of a UNiagaraScript, applied through a UUpgradeNiagaraScriptResults. */
UCLASS(BlueprintInternalUseOnly)
class UNiagaraPythonScriptModuleInput : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraPythonScriptModuleInput() {};

	UPROPERTY()
	const UNiagaraClipboardFunctionInput* Input = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    bool IsSet() const;
	
	UFUNCTION(BlueprintCallable, Category = "Scripting")
    bool IsLocalValue() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    float AsFloat() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    int32 AsInt() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    bool AsBool() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FVector2D AsVec2() const;
    
	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FVector AsVec3() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FVector4 AsVec4() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FLinearColor AsColor() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FQuat AsQuat() const;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    FString AsEnum() const;
};

/**
 * Wrapper class for passing results back from the version upgrade python script.
 */
UCLASS(BlueprintType)
class NIAGARAEDITOR_API UUpgradeNiagaraScriptResults : public UObject
{
	GENERATED_BODY()

public:

	UUpgradeNiagaraScriptResults();

	void Init();
	
	// Whether the converter process was cancelled due to an unrecoverable error in the python script process.
	UPROPERTY(BlueprintReadWrite, Category = "Scripting")
	bool bCancelledByPythonError;

	UPROPERTY(BlueprintReadWrite, Category = "Scripting")
	TArray<UNiagaraPythonScriptModuleInput*> OldInputs;

	UPROPERTY(BlueprintReadWrite, Category = "Scripting")
	TArray<UNiagaraPythonScriptModuleInput*> NewInputs;

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    UNiagaraPythonScriptModuleInput* GetOldInput(const FString& InputName);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetFloatInput(const FString& InputName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetIntInput(const FString& InputName, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetBoolInput(const FString& InputName, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetVec2Input(const FString& InputName, FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetVec3Input(const FString& InputName, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetVec4Input(const FString& InputName, FVector4 Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetColorInput(const FString& InputName, FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetQuatInput(const FString& InputName, FQuat Value);

	UFUNCTION(BlueprintCallable, Category = "Scripting")
    void SetEnumInput(const FString& InputName, FString Value);

private:
	UNiagaraPythonScriptModuleInput* GetNewInput(const FName& InputName) const;

	// This is used as a placeholder object for python to interact with when a module input could not be found. Returning a nullptr instead would crash the script.
	UPROPERTY(Transient)
	UNiagaraPythonScriptModuleInput* DummyInput;
};

struct NIAGARAEDITOR_API FNiagaraScriptVersionUpgradeContext {
	TFunction<void(UNiagaraClipboardContent*)> CreateClipboardCallback;
	TFunction<void(UNiagaraClipboardContent*, FText&)> ApplyClipboardCallback;
	FCompileConstantResolver ConstantResolver;
	bool bSkipPythonScript = false;
};
