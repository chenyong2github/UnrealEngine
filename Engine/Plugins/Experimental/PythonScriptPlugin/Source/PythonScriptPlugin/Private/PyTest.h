// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PyTest.generated.h"

/**
 * Delegate to allow testing of the various script delegate features that are exposed to Python wrapped types.
 */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(int32, FPyTestDelegate, int32, InValue);

/**
 * Multicast delegate to allow testing of the various script delegate features that are exposed to Python wrapped types.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPyTestMulticastDelegate, FString, InStr);

/**
 * Enum to allow testing of the various UEnum features that are exposed to Python wrapped types.
 */
UENUM(BlueprintType)
enum class EPyTestEnum : uint8
{
	One,
	Two,
};

/**
 * Struct to allow testing of the various UStruct features that are exposed to Python wrapped types.
 */
USTRUCT(BlueprintType)
struct FPyTestStruct
{
	GENERATED_BODY()

public:
	FPyTestStruct();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	bool Bool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	int32 Int;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	float Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	EPyTestEnum Enum;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FString String;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TArray<FString> StringArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TSet<FString> StringSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TMap<FString, int32> StringIntMap;

	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="LegacyInt is deprecated. Please use Int instead."))
	int32 LegacyInt_DEPRECATED;

	UPROPERTY(EditInstanceOnly, Category = "Python|Internal")
	bool BoolInstanceOnly;

	UPROPERTY(EditDefaultsOnly, Category = "Python|Internal")
	bool BoolDefaultsOnly;
};

/**
 * Struct to allow testing of inheritance on Python wrapped types.
 */
USTRUCT(BlueprintType)
struct FPyTestChildStruct : public FPyTestStruct
{
	GENERATED_BODY()
};

/**
 * Function library containing methods that should be hoisted onto the test struct in Python.
 */
UCLASS()
class UPyTestStructLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod="IsBoolSet;IsBoolSetOld"))
	static bool IsBoolSet(const FPyTestStruct& InStruct);

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod, DeprecatedFunction, DeprecationMessage="LegacyIsBoolSet is deprecated. Please use IsBoolSet instead."))
	static bool LegacyIsBoolSet(const FPyTestStruct& InStruct);

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptConstant="ConstantValue", ScriptConstantHost="PyTestStruct"))
	static int32 GetConstantValue();

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod, ScriptMethodSelfReturn, ScriptOperator="+;+="))
	static FPyTestStruct AddInt(const FPyTestStruct& InStruct, const int32 InValue);

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod, ScriptMethodSelfReturn, ScriptOperator="+;+="))
	static FPyTestStruct AddFloat(const FPyTestStruct& InStruct, const float InValue);

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod, ScriptMethodSelfReturn, ScriptOperator="+;+="))
	static FPyTestStruct AddStr(const FPyTestStruct& InStruct, const FString& InValue);
};

/**
 * Object to allow testing of the various UObject features that are exposed to Python wrapped types.
 */
UCLASS(Blueprintable)
class UPyTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPyTestObject();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	bool Bool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	int32 Int;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	float Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	EPyTestEnum Enum;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FString String;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TArray<FString> StringArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TSet<FString> StringSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TMap<FString, int32> StringIntMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FPyTestDelegate Delegate;

	UPROPERTY(EditAnywhere, BlueprintAssignable, Category = "Python|Internal")
	FPyTestMulticastDelegate MulticastDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FPyTestStruct Struct;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	TArray<FPyTestStruct> StructArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python|Internal")
	FPyTestChildStruct ChildStruct;

	UPROPERTY(EditInstanceOnly, Category = "Python|Internal")
	bool BoolInstanceOnly;

	UPROPERTY(EditDefaultsOnly, Category = "Python|Internal")
	bool BoolDefaultsOnly;

	UFUNCTION(BlueprintImplementableEvent, Category = "Python|Internal")
	int32 FuncBlueprintImplementable(const int32 InValue) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Python|Internal")
	int32 FuncBlueprintNative(const int32 InValue) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Python|Internal")
	void FuncBlueprintNativeRef(UPARAM(ref) FPyTestStruct& InOutStruct) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	int32 CallFuncBlueprintImplementable(const int32 InValue) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	int32 CallFuncBlueprintNative(const int32 InValue) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	void CallFuncBlueprintNativeRef(UPARAM(ref) FPyTestStruct& InOutStruct) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	void FuncTakingPyTestStruct(const FPyTestStruct& InStruct) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	void FuncTakingPyTestChildStruct(const FPyTestChildStruct& InStruct) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal", meta=(DeprecatedFunction, DeprecationMessage="LegacyFuncTakingPyTestStruct is deprecated. Please use FuncTakingPyTestStruct instead."))
	void LegacyFuncTakingPyTestStruct(const FPyTestStruct& InStruct) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	int32 FuncTakingPyTestDelegate(const FPyTestDelegate& InDelegate, const int32 InValue) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	int32 DelegatePropertyCallback(const int32 InValue) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	void MulticastDelegatePropertyCallback(FString InStr) const;

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	static TArray<int32> ReturnArray();

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	static TSet<int32> ReturnSet();

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	static TMap<int32, bool> ReturnMap();

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	static void EmitScriptError();

	UFUNCTION(BlueprintCallable, Category = "Python|Internal")
	static void EmitScriptWarning();

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptConstant="ConstantValue"))
	static int32 GetConstantValue();
};

/**
 * Object to allow testing of inheritance on Python wrapped types.
 */
UCLASS(Blueprintable)
class UPyTestChildObject : public UPyTestObject
{
	GENERATED_BODY()
};

/**
 * Object to test deprecation of Python wrapped types.
 */
UCLASS(Blueprintable, deprecated, meta=(DeprecationMessage="LegacyPyTestObject is deprecated. Please use PyTestObject instead."))
class UDEPRECATED_LegacyPyTestObject : public UPyTestObject
{
	GENERATED_BODY()
};

/**
 * Function library containing methods that should be hoisted onto the test object in Python.
 */
UCLASS()
class UPyTestObjectLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptMethod="IsBoolSet"))
	static bool IsBoolSet(const UPyTestObject* InObj);

	UFUNCTION(BlueprintPure, Category = "Python|Internal", meta=(ScriptConstant="OtherConstantValue", ScriptConstantHost="PyTestObject"))
	static int32 GetOtherConstantValue();
};
