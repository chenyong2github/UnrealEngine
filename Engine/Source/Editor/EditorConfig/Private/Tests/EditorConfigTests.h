// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "EditorConfigTests.generated.h"

USTRUCT()
struct FEditorConfigTestStruct
{
	GENERATED_BODY()

	UPROPERTY()
	bool BoolProperty = false;

	UPROPERTY()
	int32 IntProperty = 0;

	UPROPERTY()
	FString StringProperty;

	UPROPERTY()
	float FloatProperty = 0.0f;

	UPROPERTY()
	TArray<FString> ArrayProperty;
};

USTRUCT()
struct FEditorConfigTestKeyStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	FString Name;
	
	UPROPERTY()
	double Number = 0.0;

	bool operator==(const FEditorConfigTestKeyStruct& Other) const;
	bool operator!=(const FEditorConfigTestKeyStruct& Other) const;
};

FORCEINLINE uint32 GetTypeHash(const FEditorConfigTestKeyStruct& Struct)
{
	return HashCombine(GetTypeHash(Struct.Name), GetTypeHash(Struct.Number));
}

USTRUCT()
struct FEditorConfigTestComplexArrayStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FEditorConfigTestKeyStruct> Array;
};

USTRUCT()
struct FEditorConfigTestSimpleMapStruct
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, FString> Map;
};

USTRUCT()
struct FEditorConfigTestComplexMapStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	TMap<FEditorConfigTestKeyStruct, FEditorConfigTestKeyStruct> Map;
};

USTRUCT()
struct FEditorConfigTestSimpleSetStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	TSet<FName> Set;
};

USTRUCT()
struct FEditorConfigTestComplexSetStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	TSet<FEditorConfigTestKeyStruct> Set;
};

UCLASS()
class UEditorConfigTestObject : public UObject
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> Object;

	UPROPERTY()
	FSoftObjectPath SoftObjectPath;

	UPROPERTY()
	FEditorConfigTestStruct Struct;

	UPROPERTY()
	int32 Number;
};
