// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGMetadataCommon.h"
#include "PCGPoint.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGMetadataAccessor.generated.h"

class UPCGMetadata;

UCLASS()
class PCG_API UPCGMetadataAccessorHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Id-based metadata functions */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static float GetFloatAttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetFloatAttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, float Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static FVector GetVectorAttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetVectorAttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, const FVector& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static FVector4 GetVector4AttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetVector4AttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, const FVector4& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static FQuat GetQuatAttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetQuatAttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, const FQuat& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static FTransform GetTransformAttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetTransformAttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, const FTransform& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static FString GetStringAttributeByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static void SetStringAttributeByMetadataKey(UPARAM(ref) int64& Key, UPCGMetadata* Metadata, FName AttributeName, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	static bool HasAttributeSetByMetadataKey(int64 Key, UPCGMetadata* Metadata, FName AttributeName);

	/** Point functions */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (ScriptMethod))
	static void CopyPoint(const FPCGPoint& InPoint, FPCGPoint& OutPoint, bool bCopyMetadata = true, const UPCGMetadata* InMetadata = nullptr, UPCGMetadata* OutMetadata = nullptr);

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
	static T GetAttribute(PCGMetadataEntryKey Key, UPCGMetadata* Metadata, FName AttributeName);

	template<typename T>
	static void SetAttribute(PCGMetadataEntryKey& Key, UPCGMetadata* Metadata, FName AttributeName, const T& Value);
};