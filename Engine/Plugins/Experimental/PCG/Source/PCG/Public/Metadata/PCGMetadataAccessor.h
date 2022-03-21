// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGMetadataCommon.h"
#include "PCGPoint.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGMetadataAccessor.generated.h"

struct FPCGPoint;
class UPCGMetadata;

UCLASS()
class PCG_API UPCGMetadataAccessorHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static void InitializeMetadata(FPCGPoint& Point, UPCGMetadata* Metadata);
	static void InitializeMetadata(FPCGPoint& Point, UPCGMetadata* Metadata, const FPCGPoint& ParentPoint, const UPCGMetadata* ParentMetadata);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void InitializeMetadata(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, const FPCGPoint& ParentPoint);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static float GetFloatAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetFloatAttribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, float Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static FVector GetVectorAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetVectorAttribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FVector& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static FVector4 GetVector4Attribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetVector4Attribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FVector4& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static FQuat GetQuatAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetQuatAttribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FQuat& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static FTransform GetTransformAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetTransformAttribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FTransform& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static FString GetStringAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static void SetStringAttribute(UPARAM(ref) FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta = (ScriptMethod))
	static bool HasAttributeSet(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

protected:
	template<typename T>
	static T GetAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName);

	template<typename T>
	static void SetAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const T& Value);
};