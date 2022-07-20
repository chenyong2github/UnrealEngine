// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGModule.h"

#include "Misc/ScopeRWLock.h"
#include "Helpers/PCGSettingsHelpers.h"

void UPCGMetadata::Serialize(FArchive& InArchive)
{
	Super::Serialize(InArchive);

	int32 NumAttributes = (InArchive.IsLoading() ? 0 : Attributes.Num());

	InArchive << NumAttributes;

	if (InArchive.IsLoading())
	{
		for (int32 AttributeIndex = 0; AttributeIndex < NumAttributes; ++AttributeIndex)
		{
			FName AttributeName = NAME_None;
			InArchive << AttributeName;

			int32 AttributeTypeId = 0;
			InArchive << AttributeTypeId;

			FPCGMetadataAttributeBase* SerializedAttribute = PCGMetadataAttribute::AllocateEmptyAttributeFromType(static_cast<int16>(AttributeTypeId));
			if (SerializedAttribute)
			{
				SerializedAttribute->Name = AttributeName;
				SerializedAttribute->Serialize(this, InArchive);
				Attributes.Add(AttributeName, SerializedAttribute);
			}
		}
	}
	else
	{
		for (auto& AttributePair : Attributes)
		{
			InArchive << AttributePair.Key;
			
			int32 AttributeTypeId = AttributePair.Value->GetTypeId();
			InArchive << AttributeTypeId;

			AttributePair.Value->Serialize(this, InArchive);
		}
	}

	InArchive << ParentKeys;

	// Finally, initialize non-serialized members
	if (InArchive.IsLoading())
	{
		NextAttributeId = Attributes.Num();
		ItemKeyOffset = (Parent ? Parent->GetItemCountForChild() : 0);
	}
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
	if (Parent || Attributes.Num() != 0)
	{
		// Already initialized; note that while that might be construed as a warning, there are legit cases where this is correct
		return;
	}

	Parent = ((InParent != this) ? InParent : nullptr);
	ItemKeyOffset = Parent ? Parent->GetItemCountForChild() : 0;
	AddAttributes(InParent);
}

void UPCGMetadata::InitializeAsCopy(const UPCGMetadata* InMetadataToCopy)
{
	if (!InMetadataToCopy)
	{
		return;
	}

	check(InMetadataToCopy);
	if (Parent || Attributes.Num() != 0)
	{
		UE_LOG(LogPCG, Error, TEXT("Metadata has already been initialized or already contains attributes"));
		return;
	}

	Parent = InMetadataToCopy->Parent;
	OtherParents = InMetadataToCopy->OtherParents;
	ParentKeys = InMetadataToCopy->ParentKeys;
	ItemKeyOffset = InMetadataToCopy->ItemKeyOffset;

	// Copy attributes
	for (const TPair<FName, FPCGMetadataAttributeBase*>& OtherAttribute : InMetadataToCopy->Attributes)
	{
		CopyAttribute(OtherAttribute.Value, OtherAttribute.Key, /*bKeepParent=*/false, /*bCopyEntries=*/true, /*bCopyValues=*/true);
	}
}

void UPCGMetadata::AddAttributes(const UPCGMetadata* InOther)
{
	if (!InOther)
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
			CopyAttribute(OtherAttribute.Value, OtherAttribute.Key, /*bKeepParent=*/InOther == Parent, /*bCopyEntries=*/false, /*bCopyValues=*/false);
		}
	}

	if(InOther != Parent)
	{
		OtherParents.Add(InOther);
	}
}

void UPCGMetadata::AddAttribute(const UPCGMetadata* InOther, FName AttributeName)
{
	if (!InOther || !InOther->HasAttribute(AttributeName) || HasAttribute(AttributeName))
	{
		return;
	}

	CopyAttribute(InOther->GetConstAttribute(AttributeName), AttributeName, /*bKeepParent=*/false, /*bCopyEntries=*/false, /*bCopyValues=*/false);
	OtherParents.Add(InOther);
}

void UPCGMetadata::CopyAttributes(const UPCGMetadata* InOther)
{
	if (!InOther || InOther == Parent)
	{
		return;
	}

	if (GetItemCountForChild() != InOther->GetItemCountForChild())
	{
		UE_LOG(LogPCG, Error, TEXT("Mismatch in copy attributes since the entries do not match"));
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
			CopyAttribute(OtherAttribute.Value, OtherAttribute.Key, /*bKeepParent=*/false, /*bCopyEntries=*/true, /*bCopyValues=*/true);
		}
	}
}

void UPCGMetadata::CopyAttribute(const UPCGMetadata* InOther, FName AttributeToCopy, FName NewAttributeName)
{
	if (!InOther)
	{
		return;
	}
	else if (HasAttribute(NewAttributeName) || !InOther->HasAttribute(AttributeToCopy))
	{
		return;
	}
	else if (InOther == Parent)
	{
		CopyExistingAttribute(AttributeToCopy, NewAttributeName);
		return;
	}

	if (GetItemCountForChild() != InOther->GetItemCountForChild())
	{
		UE_LOG(LogPCG, Error, TEXT("Mismatch in copy attributes since the entries do not match"));
		return;
	}

	CopyAttribute(InOther->GetConstAttribute(AttributeToCopy), NewAttributeName, /*bKeepParent=*/false, /*bCopyEntries=*/true, /*bCopyValues=*/true);
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
}

void UPCGMetadata::RemoveAttributeInternal(FName AttributeName)
{
	Attributes.Remove(AttributeName);
}

template<typename T>
FPCGMetadataAttributeBase* UPCGMetadata::CreateAttribute(FName AttributeName, const T& DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	const FPCGMetadataAttributeBase* ParentAttribute = nullptr;

	if (bOverrideParent && Parent.IsValid())
	{
		ParentAttribute = Parent->GetConstAttribute(AttributeName);
	}

	FPCGMetadataAttributeBase* NewAttribute = new FPCGMetadataAttribute<T>(this, AttributeName, ParentAttribute, DefaultValue, bAllowsInterpolation);

	AttributeLock.WriteLock();
	if (FPCGMetadataAttributeBase** ExistingAttribute = Attributes.Find(AttributeName))
	{
		UE_LOG(LogPCG, Warning, TEXT("Attribute %s already exists"), *AttributeName.ToString());
		delete NewAttribute;
		NewAttribute = *ExistingAttribute;
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

	return Attribute;
}

const FPCGMetadataAttributeBase* UPCGMetadata::GetConstAttributeById(int32 InAttributeId) const
{
	const FPCGMetadataAttributeBase* Attribute = nullptr;

	AttributeLock.ReadLock();
	for (const auto& AttributePair : Attributes)
	{
		if (AttributePair.Value && AttributePair.Value->AttributeId == InAttributeId)
		{
			Attribute = AttributePair.Value;
			break;
		}
	}
	AttributeLock.ReadUnlock();

	return Attribute;
}

bool UPCGMetadata::HasAttribute(FName AttributeName) const
{
	FReadScopeLock ScopeLock(AttributeLock);
	return Attributes.Contains(AttributeName);
}

int32 UPCGMetadata::GetAttributeCount() const
{
	FReadScopeLock ScopeLock(AttributeLock);
	return Attributes.Num();
}

void UPCGMetadata::GetAttributes(TArray<FName>& AttributeNames, TArray<EPCGMetadataTypes>& AttributeTypes) const
{
	AttributeNames.Reset();
	AttributeTypes.Reset();

	AttributeLock.ReadLock();
	for (const TPair<FName, FPCGMetadataAttributeBase*>& Attribute : Attributes)
	{
		check(Attribute.Value && Attribute.Value->Name == Attribute.Key);
		AttributeNames.Add(Attribute.Key);

		if (Attribute.Value->GetTypeId() < static_cast<uint16>(EPCGMetadataTypes::Unknown))
		{
			AttributeTypes.Add(static_cast<EPCGMetadataTypes>(Attribute.Value->GetTypeId()));
		}
		else
		{
			AttributeTypes.Add(EPCGMetadataTypes::Unknown);
		}
	}
	AttributeLock.ReadUnlock();
}

FName UPCGMetadata::GetSingleAttributeNameOrNone() const
{
	FName SingleAttributeName = NAME_None;
	AttributeLock.ReadLock();
	if (Attributes.Num() == 1)
	{
		TArray<FName, TInlineAllocator<1>> Keys;
		Attributes.GenerateKeyArray(Keys);
		check(Keys.Num() == 1);
		SingleAttributeName = Keys[0];
	}
	AttributeLock.ReadUnlock();

	return SingleAttributeName;
}

bool UPCGMetadata::ParentHasAttribute(FName AttributeName) const
{
	return Parent && Parent->HasAttribute(AttributeName);
}

void UPCGMetadata::CreateInteger64Attribute(FName AttributeName, int64 DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<int64>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateFloatAttribute(FName AttributeName, float DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<float>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateDoubleAttribute(FName AttributeName, double DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<double>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateVectorAttribute(FName AttributeName, FVector DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FVector>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateVector4Attribute(FName AttributeName, FVector4 DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FVector4>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateRotatorAttribute(FName AttributeName, FRotator DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FRotator>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateQuatAttribute(FName AttributeName, FQuat DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FQuat>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateTransformAttribute(FName AttributeName, FTransform DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FTransform>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

void UPCGMetadata::CreateStringAttribute(FName AttributeName, FString DefaultValue, bool bAllowsInterpolation, bool bOverrideParent)
{
	CreateAttribute<FString>(AttributeName, DefaultValue, bAllowsInterpolation, bOverrideParent);
}

bool UPCGMetadata::SetAttributeFromProperty(FName AttributeName, PCGMetadataEntryKey& EntryKey, const UObject* Object, const FProperty* InProperty, bool bCreate)
{
	if (!InProperty || !Object)
	{
		return false;
	}

	// Check if an attribute already exists or not if we ask to create a new one
	if (!bCreate && !HasAttribute(AttributeName))
	{
		return false;
	}

	auto CreateAttributeAndSet = [&AttributeName, this, bCreate, &EntryKey](auto&& PropertyValue) -> bool
	{
		using PropertyType = std::remove_const_t<std::remove_reference_t<decltype(PropertyValue)>>;

		FPCGMetadataAttributeBase* BaseAttribute = GetMutableAttribute(AttributeName);

		if (!BaseAttribute && bCreate)
		{
			// Interpolation is disabled and no parent override.
			BaseAttribute = CreateAttribute<PropertyType>(AttributeName, PropertyValue, false, false);
		}

		if (!BaseAttribute)
		{
			return false;
		}

		// Check that the property match the attribute type!
		if (PCG::Private::MetadataTypes<PropertyType>::Id != BaseAttribute->GetTypeId())
		{
			return false;
		}

		FPCGMetadataAttribute<PropertyType>* Attribute = static_cast<FPCGMetadataAttribute<PropertyType>*>(BaseAttribute);
		Attribute->SetValue(EntryKey, PropertyValue);

		return true;
	};

	return PCGSettingsHelpers::GetPropertyValueWithCallback(Object, InProperty, CreateAttributeAndSet);
}

void UPCGMetadata::CopyExistingAttribute(FName AttributeToCopy, FName NewAttributeName, bool bKeepParent)
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
	NewAttribute->AttributeId = NextAttributeId++;
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

bool UPCGMetadata::InitializeOnSet(PCGMetadataEntryKey& InKey, PCGMetadataEntryKey InParentKeyA, const UPCGMetadata* InParentMetadataA, PCGMetadataEntryKey InParentKeyB, const UPCGMetadata* InParentMetadataB)
{
	if (InKey == PCGInvalidEntryKey)
	{
		if (InParentKeyA != PCGInvalidEntryKey && Parent == InParentMetadataA)
		{
			InKey = AddEntry(InParentKeyA);
			return true;
		}
		else if (InParentKeyB != PCGInvalidEntryKey && Parent == InParentMetadataB)
		{
			InKey = AddEntry(InParentKeyB);
			return true;
		}
		else
		{
			InKey = AddEntry();
			return false;
		}
	}
	else if(InKey < ItemKeyOffset)
	{
		InKey = AddEntry(InKey);
		return false;
	}
	else
	{
		return false;
	}
}

PCGMetadataEntryKey UPCGMetadata::GetParentKey(PCGMetadataEntryKey LocalItemKey) const
{
	if (LocalItemKey < ItemKeyOffset)
	{
		// Key is already in parent referential
		return LocalItemKey;
	}
	else
	{
		FReadScopeLock ScopeLock(ItemLock);
		if (LocalItemKey - ItemKeyOffset < ParentKeys.Num())
		{
			return ParentKeys[LocalItemKey - ItemKeyOffset];
		}
		else
		{
			UE_LOG(LogPCG, Warning, TEXT("Invalid metadata key - check for entry key not properly initialized"));
			return PCGInvalidEntryKey;
		}
	}
}

void UPCGMetadata::MergePointAttributes(const FPCGPoint& InPointA, const FPCGPoint& InPointB, FPCGPoint& OutPoint, EPCGMetadataOp Op)
{
	MergeAttributes(InPointA.MetadataEntry, this, InPointB.MetadataEntry, this, OutPoint.MetadataEntry, Op);
}

void UPCGMetadata::MergePointAttributesSubset(const FPCGPoint& InPointA, const UPCGMetadata* InMetadataA, const UPCGMetadata* InMetadataSubsetA, const FPCGPoint& InPointB, const UPCGMetadata* InMetadataB, const UPCGMetadata* InMetadataSubsetB, FPCGPoint& OutPoint, EPCGMetadataOp Op)
{
	MergeAttributesSubset(InPointA.MetadataEntry, InMetadataA, InMetadataSubsetA, InPointB.MetadataEntry, InMetadataB, InMetadataSubsetB, OutPoint.MetadataEntry, Op);
}

void UPCGMetadata::MergeAttributes(PCGMetadataEntryKey InKeyA, const UPCGMetadata* InMetadataA, PCGMetadataEntryKey InKeyB, const UPCGMetadata* InMetadataB, PCGMetadataEntryKey& OutKey, EPCGMetadataOp Op)
{
	MergeAttributesSubset(InKeyA, InMetadataA, InMetadataA, InKeyB, InMetadataB, InMetadataB, OutKey, Op);
}

void UPCGMetadata::MergeAttributesSubset(PCGMetadataEntryKey InKeyA, const UPCGMetadata* InMetadataA, const UPCGMetadata* InMetadataSubsetA, PCGMetadataEntryKey InKeyB, const UPCGMetadata* InMetadataB, const UPCGMetadata* InMetadataSubsetB, PCGMetadataEntryKey& OutKey, EPCGMetadataOp Op)
{
	// Early out: nothing to do if both input metadata are null / points have no assigned metadata
	if (!InMetadataA && !InMetadataB)
	{
		return;
	}

	// For each attribute in the current metadata, query the values from point A & B, apply operation on the result and finally store in the out point.
	InitializeOnSet(OutKey, InKeyA, InMetadataA, InKeyB, InMetadataB);

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		const FName& AttributeName = AttributePair.Key;
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;

		// Get attribute from A
		const FPCGMetadataAttributeBase* AttributeA = nullptr;
		if (InMetadataA && ((InMetadataA == InMetadataSubsetA) || (InMetadataSubsetA && InMetadataSubsetA->HasAttribute(AttributeName))))
		{
			AttributeA = InMetadataA->GetConstAttribute(AttributeName);
		}

		if (AttributeA && AttributeA->GetTypeId() != Attribute->GetTypeId())
		{
			UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
			AttributeA = nullptr;
		}

		// Get attribute from B
		const FPCGMetadataAttributeBase* AttributeB = nullptr;
		if (InMetadataB && ((InMetadataB == InMetadataSubsetB) || (InMetadataSubsetB && InMetadataSubsetB->HasAttribute(AttributeName))))
		{
			AttributeB = InMetadataB->GetConstAttribute(AttributeName);
		}

		if (AttributeB && AttributeB->GetTypeId() != Attribute->GetTypeId())
		{
			UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
			AttributeB = nullptr;
		}

		if (AttributeA || AttributeB)
		{
			Attribute->SetValue(OutKey, AttributeA, InKeyA, AttributeB, InKeyB, Op);
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::ResetWeightedAttributes(PCGMetadataEntryKey& OutKey)
{
	InitializeOnSet(OutKey);

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;
		
		if (Attribute && Attribute->AllowsInterpolation())
		{
			Attribute->SetZeroValue(OutKey);
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::AccumulateWeightedAttributes(PCGMetadataEntryKey InKey, const UPCGMetadata* InMetadata, float Weight, bool bSetNonInterpolableAttributes, PCGMetadataEntryKey& OutKey)
{
	if (!InMetadata)
	{
		return;
	}

	bool bHasSetParent = InitializeOnSet(OutKey, InKey, InMetadata);

	const bool bShouldSetNonInterpolableAttributes = bSetNonInterpolableAttributes && !bHasSetParent;

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		const FName& AttributeName = AttributePair.Key;
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != Attribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			if (Attribute->AllowsInterpolation())
			{
				Attribute->AccumulateValue(OutKey, OtherAttribute, InKey, Weight);
			}
			else if (bShouldSetNonInterpolableAttributes)
			{
				Attribute->SetValue(OutKey, OtherAttribute, InKey);
			}
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::SetAttributes(PCGMetadataEntryKey InKey, const UPCGMetadata* InMetadata, PCGMetadataEntryKey& OutKey)
{
	if (!InMetadata)
	{
		return;
	}

	if (InitializeOnSet(OutKey, InKey, InMetadata))
	{
		// Early out; we don't need to do anything else at this point
		return;
	}

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		const FName& AttributeName = AttributePair.Key;
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != Attribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			Attribute->SetValue(OutKey, OtherAttribute, InKey);
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::SetPointAttributes(const TArrayView<const FPCGPoint>& InPoints, const UPCGMetadata* InMetadata, const TArrayView<FPCGPoint>& OutPoints)
{
	if (!InMetadata || InMetadata->GetAttributeCount() == 0)
	{
		return;
	}

	check(InPoints.Num() == OutPoints.Num());

	for (int32 PointIndex = 0; PointIndex < InPoints.Num(); ++PointIndex)
	{
		const FPCGPoint& InPoint = InPoints[PointIndex];
		FPCGPoint& OutPoint = OutPoints[PointIndex];
		InitializeOnSet(OutPoint.MetadataEntry, InPoint.MetadataEntry, InMetadata);
	}

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		const FName& AttributeName = AttributePair.Key;
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != Attribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			for (int32 PointIndex = 0; PointIndex < InPoints.Num(); ++PointIndex)
			{
				const FPCGPoint& InPoint = InPoints[PointIndex];
				FPCGPoint& OutPoint = OutPoints[PointIndex];

				Attribute->SetValue(OutPoint.MetadataEntry, OtherAttribute, InPoint.MetadataEntry);
			}
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::SetAttributes(const TArrayView<PCGMetadataEntryKey>& InKeys, const UPCGMetadata* InMetadata, const TArrayView<PCGMetadataEntryKey>& OutKeys)
{
	if (!InMetadata)
	{
		return;
	}

	check(InKeys.Num() == OutKeys.Num());

	for (int32 KeyIndex = 0; KeyIndex < InKeys.Num(); ++KeyIndex)
	{
		InitializeOnSet(OutKeys[KeyIndex], InKeys[KeyIndex], InMetadata);
	}

	AttributeLock.ReadLock();
	for(const TPair<FName, FPCGMetadataAttributeBase*>& AttributePair : Attributes)
	{
		const FName& AttributeName = AttributePair.Key;
		FPCGMetadataAttributeBase* Attribute = AttributePair.Value;

		if (const FPCGMetadataAttributeBase* OtherAttribute = InMetadata->GetConstAttribute(AttributeName))
		{
			if (OtherAttribute->GetTypeId() != Attribute->GetTypeId())
			{
				UE_LOG(LogPCG, Error, TEXT("Metadata type mismatch with attribute %s"), *AttributeName.ToString());
				continue;
			}

			for (int32 KeyIndex = 0; KeyIndex < InKeys.Num(); ++KeyIndex)
			{
				Attribute->SetValue(OutKeys[KeyIndex], OtherAttribute, InKeys[KeyIndex]);
			}
		}
	}
	AttributeLock.ReadUnlock();
}

void UPCGMetadata::MergeAttributesByKey(int64 KeyA, const UPCGMetadata* MetadataA, int64 KeyB, const UPCGMetadata* MetadataB, int64 TargetKey, EPCGMetadataOp Op, int64& OutKey)
{
	OutKey = TargetKey;
	MergeAttributes(KeyA, MetadataA, KeyB, MetadataB, OutKey, Op);
}

void UPCGMetadata::SetAttributesByKey(int64 Key, const UPCGMetadata* Metadata, int64 TargetKey, int64& OutKey)
{
	OutKey = TargetKey;
	SetAttributes(Key, Metadata, OutKey);
}

void UPCGMetadata::ResetWeightedAttributesByKey(int64 TargetKey, int64& OutKey)
{
	OutKey = TargetKey;
	ResetWeightedAttributes(OutKey);
}

void UPCGMetadata::AccumulateWeightedAttributesByKey(PCGMetadataEntryKey Key, const UPCGMetadata* Metadata, float Weight, bool bSetNonInterpolableAttributes, int64 TargetKey, int64& OutKey)
{
	OutKey = TargetKey;
	AccumulateWeightedAttributes(Key, Metadata, Weight, bSetNonInterpolableAttributes, OutKey);
}

void UPCGMetadata::MergePointAttributes(const FPCGPoint& PointA, const UPCGMetadata* MetadataA, const FPCGPoint& PointB, const UPCGMetadata* MetadataB, UPARAM(ref) FPCGPoint& TargetPoint, EPCGMetadataOp Op)
{
	MergeAttributes(PointA.MetadataEntry, MetadataA, PointB.MetadataEntry, MetadataB, TargetPoint.MetadataEntry, Op);
}

void UPCGMetadata::SetPointAttributes(const FPCGPoint& Point, const UPCGMetadata* Metadata, FPCGPoint& OutPoint)
{
	SetAttributes(Point.MetadataEntry, Metadata, OutPoint.MetadataEntry);
}

void UPCGMetadata::ResetPointWeightedAttributes(FPCGPoint& OutPoint)
{
	ResetWeightedAttributes(OutPoint.MetadataEntry);
}

void UPCGMetadata::AccumulatePointWeightedAttributes(const FPCGPoint& InPoint, const UPCGMetadata* InMetadata, float Weight, bool bSetNonInterpolableAttributes, FPCGPoint& OutPoint)
{
	AccumulateWeightedAttributes(InPoint.MetadataEntry, InMetadata, Weight, bSetNonInterpolableAttributes, OutPoint.MetadataEntry);
}
