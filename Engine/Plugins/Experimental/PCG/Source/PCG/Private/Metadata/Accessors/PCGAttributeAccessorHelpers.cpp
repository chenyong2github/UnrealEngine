// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "PCGPoint.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/PCGPropertyAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

namespace PCGAttributeAccessorHelpers
{
	void ExtractMetadataAtribute(UPCGData* InData, FName Name, UPCGMetadata*& OutMetadata, FPCGMetadataAttributeBase*& OutAttribute)
	{
		OutMetadata = nullptr;
		OutAttribute = nullptr;

		if (UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(InData))
		{
			OutMetadata = SpatialData->Metadata;
		}
		else if (UPCGParamData* ParamData = Cast<UPCGParamData>(InData))
		{
			OutMetadata = ParamData->Metadata;
		}

		if (OutMetadata)
		{
			OutAttribute = OutMetadata->GetMutableAttribute(Name);
		}
	}

	void ExtractMetadataAtribute(const UPCGData* InData, FName Name, UPCGMetadata const*& OutMetadata, FPCGMetadataAttributeBase const*& OutAttribute)
	{
		UPCGMetadata* Metadata = nullptr;
		FPCGMetadataAttributeBase* Attribute = nullptr;
		ExtractMetadataAtribute(const_cast<UPCGData*>(InData), Name, Metadata, Attribute);
		OutMetadata = Metadata;
		OutAttribute = Attribute;
	}
}

TUniquePtr<IPCGAttributeAccessor> PCGAttributeAccessorHelpers::CreatePropertyAccessor(const FProperty* InProperty)
{
	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			return MakeUnique<FPCGNumericPropertyAccessor<double>>(NumericProperty);
		}
		else if (NumericProperty->IsInteger())
		{
			return MakeUnique<FPCGNumericPropertyAccessor<int64>>(NumericProperty);
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
	{
		return MakeUnique<FPCGPropertyAccessor<bool>>(InProperty);
	}
	else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
	{
		return MakeUnique<FPCGPropertyAccessor<FString>>(InProperty);
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
	{
		return MakeUnique<FPCGPropertyAccessor<FName>>(InProperty);
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		return MakeUnique<FPCGEnumPropertyAccessor>(EnumProperty);
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FVector>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FVector4>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FQuat>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FTransform>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FRotator>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			return MakeUnique<FPCGPropertyAccessor<FVector2D>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get())
		{
			return MakeUnique<FPCGPropertyPathAccessor<FSoftObjectPath>>(InProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
		{
			return MakeUnique<FPCGPropertyPathAccessor<FSoftClassPath>>(InProperty);
		}
	}

	return TUniquePtr<IPCGAttributeAccessor>();
}

TUniquePtr<const IPCGAttributeAccessor> PCGAttributeAccessorHelpers::CreateConstAccessor(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	FName Name = InSelector.GetName();

	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
		{
			if (const FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				return CreatePropertyAccessor(Property);
			}
			else if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				return FPCGPoint::CreateCustomPropertyAccessor(Name);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateConstAccessor] Expected to select a property but the data doesn't support this property."));
		return TUniquePtr<const IPCGAttributeAccessor>();
	}

	const UPCGMetadata* Metadata = nullptr;
	const FPCGMetadataAttributeBase* Attribute = nullptr;

	ExtractMetadataAtribute(InData, Name, Metadata, Attribute);

	auto CreateTypedAccessor = [Attribute, Metadata](auto Dummy) -> TUniquePtr<const IPCGAttributeAccessor>
	{
		using AttributeType = decltype(Dummy);
		return MakeUnique<FPCGAttributeAccessor<AttributeType>>(static_cast<const FPCGMetadataAttribute<AttributeType>*>(Attribute), Metadata);
	};

	if (Attribute && Metadata)
	{
		return PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), CreateTypedAccessor);
	}
	else
	{
		return TUniquePtr<const IPCGAttributeAccessor>();
	}
}

TUniquePtr<IPCGAttributeAccessor> PCGAttributeAccessorHelpers::CreateAccessor(UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	FName Name = InSelector.GetName();

	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
		{
			if (const FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				return CreatePropertyAccessor(Property);
			}
			else if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				return FPCGPoint::CreateCustomPropertyAccessor(Name);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateAccessor] Expected to select a property but the data doesn't support this property."));
		return TUniquePtr<IPCGAttributeAccessor>();
	}

	UPCGMetadata* Metadata = nullptr;
	FPCGMetadataAttributeBase* Attribute = nullptr;

	ExtractMetadataAtribute(InData, Name, Metadata, Attribute);

	auto CreateTypedAccessor = [Attribute, Metadata](auto Dummy) -> TUniquePtr<IPCGAttributeAccessor>
	{
		using AttributeType = decltype(Dummy);
		return MakeUnique<FPCGAttributeAccessor<AttributeType>>(static_cast<FPCGMetadataAttribute<AttributeType>*>(Attribute), Metadata);
	};

	if (Attribute && Metadata)
	{
		return PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), CreateTypedAccessor);
	}
	else
	{
		return TUniquePtr<IPCGAttributeAccessor>();
	}
}

TUniquePtr<const IPCGAttributeAccessorKeys> PCGAttributeAccessorHelpers::CreateConstKeys(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
	{
		return MakeUnique<FPCGAttributeAccessorKeysPoints>(PointData->GetPoints());
	}

	const UPCGMetadata* Metadata = nullptr;
	const FPCGMetadataAttributeBase* Attribute = nullptr;

	ExtractMetadataAtribute(InData, InSelector.GetName(), Metadata, Attribute);

	if (Attribute)
	{
		return MakeUnique<FPCGAttributeAccessorKeysEntries>(Attribute);
	}
	else
	{
		return TUniquePtr<IPCGAttributeAccessorKeys>();
	}
}

TUniquePtr<IPCGAttributeAccessorKeys> PCGAttributeAccessorHelpers::CreateKeys(UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	if (UPCGPointData* PointData = Cast<UPCGPointData>(InData))
	{
		TArrayView<FPCGPoint> View(PointData->GetMutablePoints());
		return MakeUnique<FPCGAttributeAccessorKeysPoints>(View);
	}

	UPCGMetadata* Metadata = nullptr;
	FPCGMetadataAttributeBase* Attribute = nullptr;

	ExtractMetadataAtribute(InData, InSelector.GetName(), Metadata, Attribute);

	if (Attribute)
	{
		return MakeUnique<FPCGAttributeAccessorKeysEntries>(Attribute);
	}
	else
	{
		return TUniquePtr<IPCGAttributeAccessorKeys>();
	}
}
