// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGModule.h"

#include "Misc/ScopeRWLock.h"

void UPCGMetadata::Serialize(FArchive& Ar)
{
	// TODO
}

void UPCGMetadata::BeginDestroy()
{
	FWriteScopeLock ScopeLock(AttributeLock);
	for (TPair<FName, FPCGMetadataAttributeBase*>& AttributeEntry : Attributes)
	{
		delete AttributeEntry.Value;
		AttributeEntry.Value = nullptr;
	}
	Attributes.Reset();

	Super::BeginDestroy();
}

void UPCGMetadata::Initialize(const UPCGMetadata* InParent)
{
	check(!Parent && Attributes.Num() == 0);
	Parent = (InParent != this ? InParent : nullptr);
	ItemKeyOffset = Parent ? Parent->GetItemCountForChild() : 0;
	GetAllAttributeNames(AttributeNames);
}

void UPCGMetadata::AddAttributes(const UPCGMetadata* InOther)
{
	if (!InOther || InOther == Parent)
	{
		return;
	}

	for (const TPair<FName, FPCGMetadataAttributeBase*> OtherAttribute : InOther->Attributes)
	{
		if (HasAttribute(OtherAttribute.Key))
		{
			continue;
		}
		else
		{
			CopyAttribute(OtherAttribute.Value, OtherAttribute.Key, /*bKeepParent=*/false, /*bCopyEntries=*/false, /*bCopyValues=*/false);
		}
	}

	OtherParents.Add(InOther);
}

const UPCGMetadata* UPCGMetadata::GetRoot() const
{
	if (Parent)
	{
		return Parent->GetRoot();
	}
	else
	{
		return this;
	}
}

bool UPCGMetadata::HasParent(const UPCGMetadata* InTentativeParent) const
{
	if (!InTentativeParent)
	{
		return false;
	}

	const UPCGMetadata* HierarchicalParent = Parent.Get();
	while (HierarchicalParent && HierarchicalParent != InTentativeParent)
	{
		HierarchicalParent = HierarchicalParent->Parent.Get();
	}

	return HierarchicalParent == InTentativeParent;
}

void UPCGMetadata::AddAttributeInternal(FName AttributeName, FPCGMetadataAttributeBase* Attribute)
{
	// This call assumes we have a write lock on the attribute map.
	Attributes.Add(AttributeName, Attribute);
	AttributeNames.Add(AttributeName);
	HiddenAttributes.Remove(AttributeName);
}

void UPCGMetadata::RemoveAttributeInternal(FName AttributeName)
{
	Attributes.Remove(AttributeName);
	if (!Parent || !Parent->HasAttribute(AttributeName))
	{
		AttributeNames.Remove(AttributeName);
	}
}

template<typename T>
FPCGMetadataAttributeBase* UPCGMetadata::CreateAttribute(FName AttributeName, const T& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	const FPCGMetadataAttributeBase* ParentAttribute = nullptr;

	if (bOverrideParent && Parent.IsValid())
	{
		ParentAttribute = Parent->GetConstAttribute(AttributeName);
	}

	FPCGMetadataAttribute<T>* NewAttribute = new FPCGMetadataAttribute<T>(this, AttributeName, ParentAttribute, DefaultValue, bAllowsInterpolation);

	AttributeLock.WriteLock();
	if (Attributes.Find(AttributeName))
	{
		UE_LOG(LogPCG, Warning, TEXT("Attribute %s already exists"), *AttributeName.ToString());
		delete NewAttribute;
	}
	else
	{
		NewAttribute->AttributeId = NextAttributeId++;
		AddAttributeInternal(AttributeName, NewAttribute);
	}
	AttributeLock.WriteUnlock();

	return NewAttribute;
}

FPCGMetadataAttributeBase* UPCGMetadata::GetMutableAttribute(FName AttributeName)
{
	FPCGMetadataAttributeBase* Attribute = nullptr;

	AttributeLock.ReadLock();
	if (FPCGMetadataAttributeBase** FoundAttribute = Attributes.Find(AttributeName))
	{
		Attribute = *FoundAttribute;
	}
	AttributeLock.ReadUnlock();

	if (!Attribute && ParentHasAttribute(AttributeName))
	{
		Attribute = CopyAttribute(AttributeName, AttributeName, /*bKeepParent=*/true, /*bCopyEntries=*/false, /*bCopyValues=*/false);
	}

	return Attribute;
}

const FPCGMetadataAttributeBase* UPCGMetadata::GetConstAttribute(FName AttributeName) const
{
	const FPCGMetadataAttributeBase* Attribute = nullptr;

	AttributeLock.ReadLock();
	if (const FPCGMetadataAttributeBase* const* FoundAttribute = Attributes.Find(AttributeName))
	{
		Attribute = *FoundAttribute;
	}
	AttributeLock.ReadUnlock();

	if (!Attribute && ParentHasAttribute(AttributeName))
	{
		return Parent->GetConstAttribute(AttributeName);
	}
	else
	{
		return Attribute;
	}
}

bool UPCGMetadata::HasAttribute(FName AttributeName) const
{
	FReadScopeLock ScopeLock(AttributeLock);
	return AttributeNames.Contains(AttributeName);
}

bool UPCGMetadata::ParentHasAttribute(FName AttributeName) const
{
	if (Parent && Parent->HasAttribute(AttributeName))
	{
		FReadScopeLock ScopeLock(AttributeLock);
		return !HiddenAttributes.Contains(AttributeName);
	}
	else
	{
		return false;
	}
}

void UPCGMetadata::CreateFloatAttribute(FName AttributeName, float DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<float>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateVectorAttribute(FName AttributeName, const FVector& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FVector>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateVector4Attribute(FName AttributeName, const FVector4& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FVector4>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateQuatAttribute(FName AttributeName, const FQuat& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FQuat>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateTransformAttribute(FName AttributeName, const FTransform& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FTransform>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateStringAttribute(FName AttributeName, const FString& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FString>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CopyAttribute(FName AttributeToCopy, FName NewAttributeName, bool bKeepParent)
{
	CopyAttribute(AttributeToCopy, NewAttributeName, bKeepParent, /*bCopyEntries=*/true, /*bCopyValues=*/true);
}

FPCGMetadataAttributeBase* UPCGMetadata::CopyAttribute(FName AttributeToCopy, FName NewAttributeName, bool bKeepParent, bool bCopyEntries, bool bCopyValues)
{
	const FPCGMetadataAttributeBase* OriginalAttribute = nullptr;

	AttributeLock.ReadLock();
	if (FPCGMetadataAttributeBase** AttributeFound = Attributes.Find(AttributeToCopy))
	{
		OriginalAttribute = *AttributeFound;
	}
	AttributeLock.ReadUnlock();

	if (!OriginalAttribute && Parent)
	{
		OriginalAttribute = Parent->GetConstAttribute(AttributeToCopy);
	}

	if (!OriginalAttribute)
	{
		UE_LOG(LogPCG, Warning, TEXT("Attribute %s does not exist, therefore cannot be copied"), *AttributeToCopy.ToString());
		return nullptr;
	}

	return CopyAttribute(OriginalAttribute, NewAttributeName, bKeepParent, bCopyEntries, bCopyValues);
}

FPCGMetadataAttributeBase* UPCGMetadata::CopyAttribute(const FPCGMetadataAttributeBase* OriginalAttribute, FName NewAttributeName, bool bKeepParent, bool bCopyEntries, bool bCopyValues)
{
	check(OriginalAttribute);
	check(OriginalAttribute->GetMetadata()->GetRoot() == GetRoot() || !bKeepParent);
	FPCGMetadataAttributeBase* NewAttribute = OriginalAttribute->Copy(NewAttributeName, this, bKeepParent, bCopyEntries, bCopyValues);

	AttributeLock.WriteLock();
	NewAttribute->AttributeId = ++NextAttributeId;
	AddAttributeInternal(NewAttributeName, NewAttribute);
	AttributeLock.WriteUnlock();

	return NewAttribute;
}

void UPCGMetadata::RenameAttribute(FName AttributeToRename, FName NewAttributeName)
{
	bool bRenamed = false;
	AttributeLock.WriteLock();
	if (FPCGMetadataAttributeBase** AttributeFound = Attributes.Find(AttributeToRename))
	{
		FPCGMetadataAttributeBase* Attribute = *AttributeFound;
		RemoveAttributeInternal(AttributeToRename);
		Attribute->Name = NewAttributeName;
		AddAttributeInternal(NewAttributeName, Attribute);
		
		bRenamed = true;
	}
	AttributeLock.WriteUnlock();

	if(!bRenamed && ParentHasAttribute(AttributeToRename))
	{
		// If attribute is present in the parent, we'll otherwise copy the attribute from the parent with a new name
		CopyAttribute(AttributeToRename, NewAttributeName, /*bKeepParent=*/true);
		bRenamed = true;
	}

	if (!bRenamed)
	{
		UE_LOG(LogPCG, Warning, TEXT("Attribute %s does not exist and therefore cannot be renamed"), *AttributeToRename.ToString());
	}
}

void UPCGMetadata::ClearAttribute(FName AttributeToClear)
{
	FPCGMetadataAttributeBase* Attribute = nullptr;

	AttributeLock.ReadLock();
	if (FPCGMetadataAttributeBase** AttributeFound = Attributes.Find(AttributeToClear))
	{
		Attribute = *AttributeFound;
	}
	AttributeLock.ReadUnlock();

	// If the attribute exists, then we can lose all the entries
	// If it doesn't but it exists in the parent hierarchy, then we must create a new attribute.
	if (Attribute)
	{
		Attribute->ClearEntries();
	}
	else if(ParentHasAttribute(AttributeToClear))
	{
		CopyAttribute(AttributeToClear, AttributeToClear, /*bKeepParent=*/true, /*bCopyEntries=*/false, /*bCopyValues=*/false);
	}
}

void UPCGMetadata::DeleteAttribute(FName AttributeToDelete)
{
	FPCGMetadataAttributeBase* Attribute = nullptr;

	// If it's a local attribute, then just delete it
	AttributeLock.WriteLock();
	if (FPCGMetadataAttributeBase** AttributeFound = Attributes.Find(AttributeToDelete))
	{
		Attribute = *AttributeFound;
		RemoveAttributeInternal(AttributeToDelete);
	}
	AttributeLock.WriteUnlock();

	if (Attribute)
	{
		delete Attribute;
	}
	else if(ParentHasAttribute(AttributeToDelete))
	{
		// Otherwise, if it exists in parent hierarchy, then hide it
		FWriteScopeLock ScopeLock(AttributeLock);
		HiddenAttributes.Add(AttributeToDelete);
		AttributeNames.Remove(AttributeToDelete);
	}
	else
	{
		UE_LOG(LogPCG, Verbose, TEXT("Attribute %s does not exist and therefore cannot be deleted"), *AttributeToDelete.ToString());
	}
}

int64 UPCGMetadata::GetItemCountForChild() const
{
	FReadScopeLock ScopeLock(ItemLock);
	return ParentKeys.Num() + ItemKeyOffset;
}

int64 UPCGMetadata::AddEntry(int64 ParentEntry)
{
	FWriteScopeLock ScopeLock(ItemLock);
	return ParentKeys.Add(ParentEntry) + ItemKeyOffset;
}

PCGMetadataEntryKey UPCGMetadata::GetParentKey(PCGMetadataEntryKey LocalItemKey) const
{
	if (LocalItemKey < ItemKeyOffset)
	{
		if (Parent)
		{
			return Parent->GetParentKey(LocalItemKey);
		}
		else
		{
			return PCGInvalidEntryKey;
		}
	}
	else
	{
		FReadScopeLock ScopeLock(ItemLock);
		check(LocalItemKey - ItemKeyOffset < ParentKeys.Num());
		return ParentKeys[LocalItemKey - ItemKeyOffset];
	}
}

void UPCGMetadata::GetAllAttributeNames(TSet<FName>& FoundAttributeNames) const
{
	for (const TPair<FName, FPCGMetadataAttributeBase*>& Attribute : Attributes)
	{
		FoundAttributeNames.Add(Attribute.Key);
	}

	if (Parent)
	{
		Parent->GetAllAttributeNames(FoundAttributeNames);
	}
}

void UPCGMetadata::MergeAttributes(const FPCGPoint& InPointA, const UPCGMetadata* InMetadataA, const FPCGPoint& InPointB, const UPCGMetadata* InMetadataB, FPCGPoint& OutPoint, EPCGMetadataOp Op)
{
	// Early out: nothing to do if both input metadata are null / points have no assigned metadata
	if (!InMetadataA && !InMetadataB)
	{
		return;
	}

	// For each attribute in the current metadata, query the values from point A & B, apply operation on the result and finally store in the out point.
	if (OutPoint.MetadataEntry == PCGInvalidEntryKey)
	{
		if (InPointA.MetadataEntry != PCGInvalidEntryKey && (Parent == InMetadataA || this == InMetadataA))
		{
			OutPoint.MetadataEntry = AddEntry(InPointA.MetadataEntry);
		}
		else if (InPointB.MetadataEntry != PCGInvalidEntryKey && (Parent == InMetadataB || this == InMetadataB))
		{
			OutPoint.MetadataEntry = AddEntry(InPointB.MetadataEntry);
		}
		else
		{
			OutPoint.MetadataEntry = AddEntry();
		}
	}

	//METADATA TODO: This isn't great, we're allocating memory for no reason here
	AttributeLock.ReadLock();
	TSet<FName> KnownAttributeNames = AttributeNames;
	AttributeLock.ReadUnlock();

	for(const FName& AttributeName : KnownAttributeNames)
	{
		const FPCGMetadataAttributeBase* ConstAttribute = GetConstAttribute(AttributeName);

		// Get attribute from A
		const FPCGMetadataAttributeBase* AttributeA = InMetadataA ? InMetadataA->GetConstAttribute(AttributeName) : nullptr;

		if (AttributeA && AttributeA->GetTypeId() != ConstAttribute->GetTypeId())
		{
			UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
			AttributeA = nullptr;
		}

		// Get attribute from B
		const FPCGMetadataAttributeBase* AttributeB = InMetadataB ? InMetadataB->GetConstAttribute(AttributeName) : nullptr;

		if (AttributeB && AttributeB->GetTypeId() != ConstAttribute->GetTypeId())
		{
			UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
			AttributeB = nullptr;
		}

		if (AttributeA || AttributeB)
		{
			FPCGMetadataAttributeBase* Attribute = GetMutableAttribute(AttributeName);
			if (Attribute)
			{
				Attribute->SetValue(OutPoint.MetadataEntry, AttributeA, InPointA.MetadataEntry, AttributeB, InPointB.MetadataEntry, Op);
			}
		}
	}
}

void UPCGMetadata::ResetWeightedAttributes(FPCGPoint& OutPoint)
{
	if (OutPoint.MetadataEntry == PCGInvalidEntryKey)
	{
		OutPoint.MetadataEntry = AddEntry();
	}

	//METADATA TODO: This isn't great, we're allocating memory for no reason here
	AttributeLock.ReadLock();
	TSet<FName> KnownAttributeNames = AttributeNames;
	AttributeLock.ReadUnlock();

	for (const FName& AttributeName : KnownAttributeNames)
	{
		const FPCGMetadataAttributeBase* ConstAttribute = GetConstAttribute(AttributeName);
		
		if (!ConstAttribute->AllowsInterpolation())
		{
			continue;
		}

		FPCGMetadataAttributeBase* Attribute = GetMutableAttribute(AttributeName);
		if (Attribute)
		{
			check(Attribute->AllowsInterpolation());
			Attribute->SetZeroValue(OutPoint.MetadataEntry);
		}
	}
}

void UPCGMetadata::AccumulateWeightedAttributes(const FPCGPoint& InPoint, const UPCGMetadata* InMetadata, float Weight, bool bSetNonInterpolableAttributes, FPCGPoint& OutPoint)
{
	if (!InMetadata)
	{
		return;
	}

	bool bHasSetParent = false;

	if (OutPoint.MetadataEntry == PCGInvalidEntryKey)
	{
		if (InPoint.MetadataEntry != PCGInvalidEntryKey && Parent == InMetadata)
		{
			OutPoint.MetadataEntry = AddEntry(InPoint.MetadataEntry);
			// No early out here, since we'll need to set the weighted value
			bHasSetParent = true;
		}
		else
		{
			OutPoint.MetadataEntry = AddEntry();
		}
	}

	const bool bShouldSetNonInterpolableAttributes = bSetNonInterpolableAttributes && !bHasSetParent;

	//METADATA TODO: This isn't great, we're allocating memory for no reason here
	AttributeLock.ReadLock();
	TSet<FName> KnownAttributeNames = AttributeNames;
	AttributeLock.ReadUnlock();

	for(const FName& AttributeName : KnownAttributeNames)
	{
		const FPCGMetadataAttributeBase* ConstAttribute = GetConstAttribute(AttributeName);

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != ConstAttribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			FPCGMetadataAttributeBase* Attribute = GetMutableAttribute(AttributeName);

			if (Attribute->AllowsInterpolation())
			{
				Attribute->AccumulateValue(OutPoint.MetadataEntry, OtherAttribute, InPoint.MetadataEntry, Weight);
			}
			else if (bShouldSetNonInterpolableAttributes)
			{
				Attribute->SetValue(OutPoint.MetadataEntry, OtherAttribute, InPoint.MetadataEntry);
			}
		}
	}
}

void UPCGMetadata::SetAttributes(const FPCGPoint& InPoint, const UPCGMetadata* InMetadata, FPCGPoint& OutPoint)
{
	if (!InMetadata)
	{
		return;
	}

	if (OutPoint.MetadataEntry == PCGInvalidEntryKey)
	{
		if (InPoint.MetadataEntry != PCGInvalidEntryKey && Parent == InMetadata)
		{
			OutPoint.MetadataEntry = AddEntry(InPoint.MetadataEntry);

			// Early out; we don't need to do anything else at this point
			return;
		}
		else
		{
			OutPoint.MetadataEntry = AddEntry();
		}
	}

	//METADATA TODO: This isn't great, we're allocating memory for no reason here
	AttributeLock.ReadLock();
	TSet<FName> KnownAttributeNames = AttributeNames;
	AttributeLock.ReadUnlock();

	for(const FName& AttributeName : KnownAttributeNames)
	{
		const FPCGMetadataAttributeBase* ConstAttribute = GetConstAttribute(AttributeName);

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != ConstAttribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			FPCGMetadataAttributeBase* Attribute = GetMutableAttribute(AttributeName);
			Attribute->SetValue(OutPoint.MetadataEntry, OtherAttribute, InPoint.MetadataEntry);
		}
	}
}

void UPCGMetadata::SetAttributes(const TArrayView<const FPCGPoint>& InPoints, const UPCGMetadata* InMetadata, const TArrayView<FPCGPoint>& OutPoints)
{
	if (!InMetadata)
	{
		return;
	}

	check(InPoints.Num() == OutPoints.Num());

	for (int32 PointIndex = 0; PointIndex < InPoints.Num(); ++PointIndex)
	{
		const FPCGPoint& InPoint = InPoints[PointIndex];
		FPCGPoint& OutPoint = OutPoints[PointIndex];
		if (OutPoint.MetadataEntry == PCGInvalidEntryKey)
		{
			if (InPoint.MetadataEntry != PCGInvalidEntryKey && Parent == InMetadata)
			{
				OutPoint.MetadataEntry = AddEntry(InPoint.MetadataEntry);
			}
			else
			{
				OutPoint.MetadataEntry = AddEntry();
			}
		}
	}

	//METADATA TODO: This isn't great, we're allocating memory for no reason here
	AttributeLock.ReadLock();
	TSet<FName> KnownAttributeNames = AttributeNames;
	AttributeLock.ReadUnlock();

	for(const FName& AttributeName : KnownAttributeNames)
	{
		const FPCGMetadataAttributeBase* ConstAttribute = GetConstAttribute(AttributeName);

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != ConstAttribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			FPCGMetadataAttributeBase* Attribute = GetMutableAttribute(AttributeName);

			for (int32 PointIndex = 0; PointIndex < InPoints.Num(); ++PointIndex)
			{
				const FPCGPoint& InPoint = InPoints[PointIndex];
				FPCGPoint& OutPoint = OutPoints[PointIndex];

				Attribute->SetValue(OutPoint.MetadataEntry, OtherAttribute, InPoint.MetadataEntry);
			}
		}
	}
}