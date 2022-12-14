// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "PCGParamData.h"
#include "PCGPoint.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/Accessors/PCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGPropertyAccessor.h"
#include "Metadata/Accessors/PCGAttributeExtractor.h"

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
			// If Name is None, try ot get the latest attribute
			if (Name == NAME_None)
			{
				Name = OutMetadata->GetLatestAttributeNameOrNone();
			}

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

	TUniquePtr<IPCGAttributeAccessor> CreateChainAccessor(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess)
	{
		if (!InAccessor.IsValid())
		{
			bOutSuccess = false;
			return TUniquePtr<IPCGAttributeAccessor>();
		}

		auto Chain = [&Accessor = InAccessor, Name, &bOutSuccess](auto Dummy) -> TUniquePtr<IPCGAttributeAccessor>
		{
			using AccessorType = decltype(Dummy);

			if constexpr (PCG::Private::IsOfTypes<AccessorType, FVector2D, FVector, FVector4, FQuat>())
			{
				return PCGAttributeExtractor::CreateVectorExtractor<AccessorType>(std::move(Accessor), Name, bOutSuccess);
			}
			if constexpr (PCG::Private::IsOfTypes<AccessorType, FTransform>())
			{
				return PCGAttributeExtractor::CreateTransformExtractor(std::move(Accessor), Name, bOutSuccess);
			}
			if constexpr (PCG::Private::IsOfTypes<AccessorType, FRotator>())
			{
				return PCGAttributeExtractor::CreateRotatorExtractor(std::move(Accessor), Name, bOutSuccess);
			}
			else
			{
				bOutSuccess = false;
				return std::move(Accessor);
			}
		};

		return PCGMetadataAttribute::CallbackWithRightType(InAccessor->GetUnderlyingType(), Chain);
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
		return MakeUnique<FPCGPropertyAccessor<bool, FBoolProperty>>(BoolProperty);
	}
	else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
	{
		return MakeUnique<FPCGPropertyAccessor<FString, FStrProperty>>(StringProperty);
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
	{
		return MakeUnique<FPCGPropertyAccessor<FName, FNameProperty>>(NameProperty);
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		return MakeUnique<FPCGEnumPropertyAccessor>(EnumProperty);
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FVector>>(StructProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FVector4>>(StructProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FQuat>>(StructProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FTransform>>(StructProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FRotator>>(StructProperty);
		}
		else if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			return MakeUnique<FPCGPropertyStructAccessor<FVector2D>>(StructProperty);
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
	const FName Name = InSelector.GetName();
	TUniquePtr<IPCGAttributeAccessor> Accessor;

	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
		{
			if (const FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				Accessor = CreatePropertyAccessor(Property);
			}
			else if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				Accessor = FPCGPoint::CreateCustomPropertyAccessor(Name);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty && !Accessor.IsValid())
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateConstAccessor] Expected to select a property but the data doesn't support this property."));
		return TUniquePtr<const IPCGAttributeAccessor>();
	}

	if (InSelector.Selection == EPCGAttributePropertySelection::Attribute)
	{
		const UPCGMetadata* Metadata = nullptr;
		const FPCGMetadataAttributeBase* Attribute = nullptr;

		ExtractMetadataAtribute(InData, Name, Metadata, Attribute);

		auto CreateTypedAccessor = [Attribute, Metadata](auto Dummy) -> TUniquePtr<IPCGAttributeAccessor>
		{
			using AttributeType = decltype(Dummy);
			return MakeUnique<FPCGAttributeAccessor<AttributeType>>(static_cast<const FPCGMetadataAttribute<AttributeType>*>(Attribute), Metadata);
		};

		if (Attribute && Metadata)
		{
			Accessor = PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), CreateTypedAccessor);
		}
		else
		{
			return TUniquePtr<const IPCGAttributeAccessor>();
		}
	}

	if (!Accessor.IsValid())
	{
		return TUniquePtr<const IPCGAttributeAccessor>();
	}

	// At this point, check if we have chain accessors
	for (const FString& ExtraName : InSelector.ExtraNames)
	{
		bool bSuccess = false;
		Accessor = CreateChainAccessor(std::move(Accessor), FName(ExtraName), bSuccess);
		if (!bSuccess)
		{
			UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateConstAccessor] Extra selectors don't match existing properties."));
			return TUniquePtr<const IPCGAttributeAccessor>();
		}
	}

	return Accessor;
}

TUniquePtr<IPCGAttributeAccessor> PCGAttributeAccessorHelpers::CreateAccessor(UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	const FName Name = InSelector.GetName();
	TUniquePtr<IPCGAttributeAccessor> Accessor;

	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
		{
			if (const FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				Accessor = CreatePropertyAccessor(Property);
			}
			else if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				Accessor = FPCGPoint::CreateCustomPropertyAccessor(Name);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty && !Accessor.IsValid())
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateAccessor] Expected to select a property but the data doesn't support this property."));
		return TUniquePtr<IPCGAttributeAccessor>();
	}

	if (InSelector.Selection == EPCGAttributePropertySelection::Attribute)
	{
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
			Accessor = PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), CreateTypedAccessor);
		}
		else
		{
			return TUniquePtr<IPCGAttributeAccessor>();
		}
	}

	if (!Accessor.IsValid())
	{
		return TUniquePtr<IPCGAttributeAccessor>();
	}

	// At this point, check if we have chain accessors
	for (const FString& ExtraName : InSelector.ExtraNames)
	{
		bool bSuccess = false;
		Accessor = CreateChainAccessor(std::move(Accessor), FName(ExtraName), bSuccess);
		if (!bSuccess)
		{
			UE_LOG(LogPCG, Error, TEXT("[PCGAttributeAccessorHelpers::CreateAccessor] Extra selectors don't match existing properties."));
			return TUniquePtr<IPCGAttributeAccessor>();
		}
	}

	return Accessor;
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
