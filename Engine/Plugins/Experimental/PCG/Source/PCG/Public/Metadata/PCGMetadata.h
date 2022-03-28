// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGMetadataCommon.h"
#include "PCGMetadataAccessor.h"

#include "PCGMetadata.generated.h"

class FPCGMetadataAttributeBase;

UENUM()
enum class EPCGMetadataOp : uint8
{
	Min,
	Max,
	Sub,
	Add
};

UCLASS(BlueprintType)
class PCG_API UPCGMetadata : public UObject
{
	GENERATED_BODY()

public:

	//~ Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;
	//~ End UObject interface

	/** Initializes the metadata from a parent metadata, if any (can be null) */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void Initialize(const UPCGMetadata* InParent);

	/** Creates streams for another metadata if they are not currently present */
	void AddAttributes(const UPCGMetadata* InOther);

	/** Returns this metadata's parent */
	const UPCGMetadata* GetParent() const { return Parent.Get(); }
	const UPCGMetadata* GetRoot() const;
	bool HasParent(const UPCGMetadata* InTentativeParent) const;

	/** Create new streams */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CreateFloatAttribute(FName AttributeName, float DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CreateVectorAttribute(FName AttributeName, const FVector& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata", meta=(DisplayName = "Create Vector4 Attribute"))
	void CreateVector4Attribute(FName AttributeName, const FVector4& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CreateQuatAttribute(FName AttributeName, const FQuat& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CreateTransformAttribute(FName AttributeName, const FTransform& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CreateStringAttribute(FName AttributeName, const FString& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	/** Get attributes */
	FPCGMetadataAttributeBase* GetMutableAttribute(FName AttributeName);
	const FPCGMetadataAttributeBase* GetConstAttribute(FName AttributeName) const;
	bool HasAttribute(FName AttributeName) const;

	/** Delete/Hide attribute */
	// Due to stream inheriting, we might want to consider "hiding" parent stream and deleting local streams only
	void DeleteAttribute(FName AttributeName);

	/** Copy attribute */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void CopyAttribute(FName AttributeToCopy, FName NewAttributeName, bool bKeepParent = true);

	/** Rename attribute */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void RenameAttribute(FName AttributeToRename, FName NewAttributeName);

	/** Clear/Reinit attribute */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	void ClearAttribute(FName AttributeToClear);

	/** Adds a unique entry key to the metadata */
	UFUNCTION(BlueprintCallable, Category = "PCG|Metadata")
	int64 AddEntry(int64 ParentEntryKey = -1);

	/** Initializes the metadata entry key. Returns true if key set from either parent */
	bool InitializeOnSet(PCGMetadataEntryKey& InKey, PCGMetadataEntryKey InParentKeyA = PCGInvalidEntryKey, const UPCGMetadata* InParentMetadataA = nullptr, PCGMetadataEntryKey InParentKeyB = PCGInvalidEntryKey, const UPCGMetadata* InParentMetadataB = nullptr);

	/** Metadata chaining mechanism */
	PCGMetadataEntryKey GetParentKey(PCGMetadataEntryKey LocalItemKey) const;

	/** Attributes operations */
	void MergeAttributes(PCGMetadataEntryKey InKeyA, const UPCGMetadata* InMetadataA, PCGMetadataEntryKey InKeyB, const UPCGMetadata* InMetadataB, PCGMetadataEntryKey& OutKey, EPCGMetadataOp Op);
	void MergeAttributes(PCGMetadataEntryKey InKeyA, PCGMetadataEntryKey InKeyB, PCGMetadataEntryKey& OutKey, EPCGMetadataOp Op)
	{
		MergeAttributes(InKeyA, this, InKeyB, this, OutKey, Op);
	}

	void ResetWeightedAttributes(PCGMetadataEntryKey& OutKey);
	void AccumulateWeightedAttributes(PCGMetadataEntryKey InKey, const UPCGMetadata* InMetadata, float Weight, bool bSetNonInterpolableAttributes, PCGMetadataEntryKey& OutKey);

	void SetAttributes(PCGMetadataEntryKey InKey, const UPCGMetadata* InMetadata, PCGMetadataEntryKey& OutKey);
	void SetAttributes(const TArrayView<PCGMetadataEntryKey>& InKeys, const UPCGMetadata* InMetadata, const TArrayView<PCGMetadataEntryKey>& OutKeys);

	/** Attributes operations - shorthand for points */
	void MergeAttributes(const FPCGPoint& InPointA, const FPCGPoint& InPointB, FPCGPoint& OutPoint, EPCGMetadataOp Op);
	void MergeAttributes(const FPCGPoint& InPointA, const UPCGMetadata* InMetadataA, const FPCGPoint& InPointB, const UPCGMetadata* InMetadataB, FPCGPoint& OutPoint, EPCGMetadataOp Op);

	void ResetWeightedAttributes(FPCGPoint& OutPoint);
	void AccumulateWeightedAttributes(const FPCGPoint& InPoint, const UPCGMetadata* InMetadata, float Weight, bool bSetNonInterpolableAttributes, FPCGPoint& OutPoint);

	void SetAttributes(const FPCGPoint& InPoint, const UPCGMetadata* InMetadata, FPCGPoint& OutPoint);
	void SetAttributes(const TArrayView<const FPCGPoint>& InPoints, const UPCGMetadata* InMetadata, const TArrayView<FPCGPoint>& OutPoints);

protected:
	template<typename T>
	FPCGMetadataAttributeBase* CreateAttribute(FName AttributeName, const T& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent);

	FPCGMetadataAttributeBase* CopyAttribute(FName AttributeToCopy, FName NewAttributeName, bool bKeepParent, bool bCopyEntries, bool bCopyValues);
	FPCGMetadataAttributeBase* CopyAttribute(const FPCGMetadataAttributeBase* OriginalAttribute, FName NewAttributeName, bool bKeepParent, bool bCopyEntries, bool bCopyValues);

	bool ParentHasAttribute(FName AttributeName) const;
	int64 GetItemCountForChild() const;

	void AddAttributeInternal(FName AttributeName, FPCGMetadataAttributeBase* Attribute);
	void RemoveAttributeInternal(FName AttributeName);

	UPROPERTY()
	TSoftObjectPtr<const UPCGMetadata> Parent;

	// Set of parents kept for streams relationship and GC collection
	// But otherwise not used directly
	UPROPERTY()
	TSet<TSoftObjectPtr<const UPCGMetadata>> OtherParents;

	TMap<FName, FPCGMetadataAttributeBase*> Attributes;
	PCGMetadataAttributeKey NextAttributeId = 0;

	TArray<PCGMetadataEntryKey> ParentKeys;
	int64 ItemKeyOffset = 0;

	mutable FRWLock AttributeLock;
	mutable FRWLock ItemLock;
};