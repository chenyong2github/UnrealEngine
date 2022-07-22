// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGMetadataCommon.h"
#include "PCGMetadata.h"

#include "Misc/ScopeRWLock.h"
#include "Templates/EnableIf.h"

class PCG_API FPCGMetadataAttributeBase
{
public:
	FPCGMetadataAttributeBase() = default;
	FPCGMetadataAttributeBase(UPCGMetadata* InMetadata, FName InName, const FPCGMetadataAttributeBase* InParent, bool bInAllowsInterpolation);

	virtual ~FPCGMetadataAttributeBase() = default;
	virtual void Serialize(UPCGMetadata* InMetadata, FArchive& InArchive);

	const UPCGMetadata* GetMetadata() const { return Metadata; }
	int16 GetTypeId() const { return TypeId; }

	virtual FPCGMetadataAttributeBase* Copy(FName NewName, UPCGMetadata* InMetadata, bool bKeepParent, bool bCopyEntries = true, bool bCopyValues = true) const = 0;

	virtual PCGMetadataValueKey GetValueKeyOffsetForChild() const = 0;
	virtual void SetValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, PCGMetadataEntryKey InEntryKey) = 0;
	virtual void SetZeroValue(PCGMetadataEntryKey ItemKey) = 0;
	virtual void AccumulateValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, float Weight) = 0;
	virtual void SetWeightedValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttribute, const TArrayView<TPair<PCGMetadataEntryKey, float>> InWeightedKeys) = 0;
	virtual void SetValue(PCGMetadataEntryKey ItemKey, const FPCGMetadataAttributeBase* InAttributeA, PCGMetadataEntryKey InEntryKeyA, const FPCGMetadataAttributeBase* InAttributeB, PCGMetadataEntryKey InEntryKeyB, EPCGMetadataOp Op) = 0;
	virtual bool IsEqualToDefaultValue(PCGMetadataValueKey ValueKey) const = 0;

	void SetValueFromValueKey(PCGMetadataEntryKey EntryKey, PCGMetadataValueKey ValueKey);
	PCGMetadataValueKey GetValueKey(PCGMetadataEntryKey EntryKey) const;
	bool HasNonDefaultValue(PCGMetadataEntryKey EntryKey) const;
	void ClearEntries();

	bool AllowsInterpolation() const { return bAllowsInterpolation; }

protected:
	TMap<PCGMetadataEntryKey, PCGMetadataValueKey> EntryToValueKeyMap;
	mutable FRWLock EntryMapLock;

	TObjectPtr<UPCGMetadata> Metadata = nullptr;
	const FPCGMetadataAttributeBase* Parent = nullptr;
	int16 TypeId = 0;
	bool bAllowsInterpolation = false;

public:
	FName Name = NAME_None;
	PCGMetadataAttributeKey AttributeId = -1;
};