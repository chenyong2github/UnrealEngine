// Copyright Epic Games, Inc. All Rights Reserved.

#include "PyTest.h"
#include "PyUtil.h"

FPyTestStruct::FPyTestStruct()
{
	Bool = false;
	Int = 0;
	Float = 0.0f;
	Enum = EPyTestEnum::One;
	LegacyInt_DEPRECATED = 0;
	BoolInstanceOnly = false;
	BoolDefaultsOnly = false;
}

bool UPyTestStructLibrary::IsBoolSet(const FPyTestStruct& InStruct)
{
	return InStruct.Bool;
}

bool UPyTestStructLibrary::LegacyIsBoolSet(const FPyTestStruct& InStruct)
{
	return IsBoolSet(InStruct);
}

int32 UPyTestStructLibrary::GetConstantValue()
{
	return 10;
}

FPyTestStruct UPyTestStructLibrary::AddInt(const FPyTestStruct& InStruct, const int32 InValue)
{
	FPyTestStruct Result = InStruct;
	Result.Int += InValue;
	return Result;
}

FPyTestStruct UPyTestStructLibrary::AddFloat(const FPyTestStruct& InStruct, const float InValue)
{
	FPyTestStruct Result = InStruct;
	Result.Float += InValue;
	return Result;
}

FPyTestStruct UPyTestStructLibrary::AddStr(const FPyTestStruct& InStruct, const FString& InValue)
{
	FPyTestStruct Result = InStruct;
	Result.String += InValue;
	return Result;
}

UPyTestObject::UPyTestObject()
{
	StructArray.AddDefaulted();
	StructArray.AddDefaulted();
}

int32 UPyTestObject::FuncBlueprintNative_Implementation(const int32 InValue) const
{
	return InValue;
}

void UPyTestObject::FuncBlueprintNativeRef_Implementation(FPyTestStruct& InOutStruct) const
{
}

int32 UPyTestObject::CallFuncBlueprintImplementable(const int32 InValue) const
{
	return FuncBlueprintImplementable(InValue);
}

int32 UPyTestObject::CallFuncBlueprintNative(const int32 InValue) const
{
	return FuncBlueprintNative(InValue);
}

void UPyTestObject::CallFuncBlueprintNativeRef(FPyTestStruct& InOutStruct) const
{
	return FuncBlueprintNativeRef(InOutStruct);
}

void UPyTestObject::FuncTakingPyTestStruct(const FPyTestStruct& InStruct) const
{
}

void UPyTestObject::FuncTakingPyTestChildStruct(const FPyTestChildStruct& InStruct) const
{
}

void UPyTestObject::LegacyFuncTakingPyTestStruct(const FPyTestStruct& InStruct) const
{
	FuncTakingPyTestStruct(InStruct);
}

int32 UPyTestObject::FuncTakingPyTestDelegate(const FPyTestDelegate& InDelegate, const int32 InValue) const
{
	return InDelegate.IsBound() ? InDelegate.Execute(InValue) : INDEX_NONE;
}

int32 UPyTestObject::DelegatePropertyCallback(const int32 InValue) const
{
	if (InValue != Int)
	{
		UE_LOG(LogPython, Error, TEXT("Given value (%d) did not match the Int property value (%d)"), InValue, Int);
	}

	return InValue;
}

void UPyTestObject::MulticastDelegatePropertyCallback(FString InStr) const
{
	if (InStr != String)
	{
		UE_LOG(LogPython, Error, TEXT("Given value (%s) did not match the String property value (%s)"), *InStr, *String);
	}
}

TArray<int32> UPyTestObject::ReturnArray()
{
	TArray<int32> TmpArray;
	TmpArray.Add(10);
	return TmpArray;
}

TSet<int32> UPyTestObject::ReturnSet()
{
	TSet<int32> TmpSet;
	TmpSet.Add(10);
	return TmpSet;
}

TMap<int32, bool> UPyTestObject::ReturnMap()
{
	TMap<int32, bool> TmpMap;
	TmpMap.Add(10, true);
	return TmpMap;
}

void UPyTestObject::EmitScriptError()
{
	FFrame::KismetExecutionMessage(TEXT("EmitScriptError was called"), ELogVerbosity::Error);
}

void UPyTestObject::EmitScriptWarning()
{
	FFrame::KismetExecutionMessage(TEXT("EmitScriptWarning was called"), ELogVerbosity::Warning);
}

int32 UPyTestObject::GetConstantValue()
{
	return 10;
}

bool UPyTestObjectLibrary::IsBoolSet(const UPyTestObject* InObj)
{
	return InObj->Bool;
}

int32 UPyTestObjectLibrary::GetOtherConstantValue()
{
	return 20;
}
