// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGMetadataAccessor.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGModule.h"

template<typename T>
T UPCGMetadataAccessorHelpers::GetAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	if (!Metadata)
	{
		UE_LOG(LogPCG, Error, TEXT("Source data has no metadata"));
		return T{};
	}

	const FPCGMetadataAttribute<T>* Attribute = static_cast<const FPCGMetadataAttribute<T>*>(Metadata->GetConstAttribute(AttributeName));
	if (Attribute && Attribute->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
	{
		return Attribute->GetValueFromItemKey(Point.MetadataEntry);
	}
	else if (Attribute)
	{
		UE_LOG(LogPCG, Error, TEXT("Attribute %s does not have the matching type"), *AttributeName.ToString());
		return T{};
	}
	else
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid attribute name (%s)"), *AttributeName.ToString());
		return T{};
	}
}

void UPCGMetadataAccessorHelpers::InitializeMetadata(FPCGPoint& Point, UPCGMetadata* Metadata)
{
	Point.MetadataEntry = Metadata ? Metadata->AddEntry() : PCGInvalidEntryKey;
}

void UPCGMetadataAccessorHelpers::InitializeMetadata(FPCGPoint& Point, UPCGMetadata* Metadata, const FPCGPoint& ParentPoint)
{
	Point.MetadataEntry = Metadata ? Metadata->AddEntry(ParentPoint.MetadataEntry) : PCGInvalidEntryKey;
}

void UPCGMetadataAccessorHelpers::InitializeMetadata(FPCGPoint& Point, UPCGMetadata* Metadata, const FPCGPoint& ParentPoint, const UPCGMetadata* ParentMetadata)
{
	Point.MetadataEntry = Metadata ? (Metadata->HasParent(ParentMetadata) ? Metadata->AddEntry(ParentPoint.MetadataEntry) : Metadata->AddEntry()) : PCGInvalidEntryKey;
}

template<typename T>
void UPCGMetadataAccessorHelpers::SetAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const T& Value)
{
	if (!Metadata)
	{
		UE_LOG(LogPCG, Error, TEXT("Data has no metadata; cannot write value in attribute"));
		return;
	}

	if (Point.MetadataEntry == PCGInvalidEntryKey)
	{
		// Default initialization
		Point.MetadataEntry = Metadata->AddEntry();
	}

	if (Point.MetadataEntry == PCGInvalidEntryKey)
	{
		UE_LOG(LogPCG, Error, TEXT("Metadata item has no entry, therefore can't set values"));
		return;
	}

	FPCGMetadataAttribute<T>* Attribute = static_cast<FPCGMetadataAttribute<T>*>(Metadata->GetMutableAttribute(AttributeName));
	if (Attribute && Attribute->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
	{
		Attribute->SetValue(Point.MetadataEntry, Value);
	}
	else if (Attribute)
	{
		UE_LOG(LogPCG, Error, TEXT("Attribute %s does not have the matching type"), *AttributeName.ToString());
	}
	else
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid attribute name (%s)"), *AttributeName.ToString());
	}
}

float UPCGMetadataAccessorHelpers::GetFloatAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<float>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetFloatAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, float Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

FVector UPCGMetadataAccessorHelpers::GetVectorAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<FVector>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetVectorAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FVector& Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

FVector4 UPCGMetadataAccessorHelpers::GetVector4Attribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<FVector4>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetVector4Attribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FVector4& Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

FQuat UPCGMetadataAccessorHelpers::GetQuatAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<FQuat>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetQuatAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FQuat& Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

FTransform UPCGMetadataAccessorHelpers::GetTransformAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<FTransform>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetTransformAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FTransform& Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

FString UPCGMetadataAccessorHelpers::GetStringAttribute(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	return GetAttribute<FString>(Point, Metadata, AttributeName);
}

void UPCGMetadataAccessorHelpers::SetStringAttribute(FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName, const FString& Value)
{
	SetAttribute(Point, Metadata, AttributeName, Value);
}

bool UPCGMetadataAccessorHelpers::HasAttributeSet(const FPCGPoint& Point, UPCGMetadata* Metadata, FName AttributeName)
{
	if (!Metadata)
	{
		UE_LOG(LogPCG, Error, TEXT("Data has no metadata"));
		return false;
	}

	if (!Metadata->HasAttribute(AttributeName))
	{
		UE_LOG(LogPCG, Error, TEXT("Metadata does not have a %s attribute"), *AttributeName.ToString());
		return false;
	}

	// Early out: the point has no metadata entry assigned
	if (Point.MetadataEntry == PCGInvalidEntryKey)
	{
		return false;
	}

	const FPCGMetadataAttributeBase* Attribute = Metadata->GetConstAttribute(AttributeName);
	check(Attribute);

	return Attribute->HasNonDefaultValue(Point.MetadataEntry);
}