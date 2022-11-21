// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGMetadataAttributeWrapper.h"

#include "Data/PCGSpatialData.h"
#include "Data/PCGPointData.h"
#include "PCGParamData.h"

EPCGMetadataTypes PCGMetadataAttributeWrapper::GetMetadataTypeFromProperty(const FProperty* InProperty)
{
	if (!InProperty)
	{
		return EPCGMetadataTypes::Unknown;
	}
	else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			return EPCGMetadataTypes::Double;
		}
		else if (NumericProperty->IsInteger())
		{
			return EPCGMetadataTypes::Integer64;
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
	{
		return EPCGMetadataTypes::Boolean;
	}
	else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
	{
		return EPCGMetadataTypes::String;
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
	{
		return EPCGMetadataTypes::Name;
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		return EPCGMetadataTypes::Integer64;
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			return EPCGMetadataTypes::Vector;
		}
		else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			return EPCGMetadataTypes::Vector4;
		}
		else if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			return EPCGMetadataTypes::Vector2;
		}
		else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
		{
			return EPCGMetadataTypes::Quaternion;
		}
		else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			return EPCGMetadataTypes::Transform;
		}
		else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			return EPCGMetadataTypes::Rotator;
		}
		else if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get() || StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
		{
			// Soft object path are transformed from strings
			return EPCGMetadataTypes::String;
		}
		//else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
		//else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
	{
		// Object are transformed into their soft path name (as a string attribute)
		return EPCGMetadataTypes::String;
	}

	return EPCGMetadataTypes::Unknown;
}

bool PCGMetadataAttributeWrapper::IsPropertyWithType(const UPCGData* InData, FName InName, int16* OutType)
{
	if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
	{
		if (const FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(InName))
		{
			if (OutType)
			{
				*OutType = (int16)GetMetadataTypeFromProperty(Property);
			}

			return true;
		}
		else if (FPCGPoint::HasCustomPropertyGetterSetter(InName))
		{
			if (OutType)
			{
				*OutType = FPCGPoint::CreateCustomPropertyGetterSetter(InName).GetType();
			}
			return true;
		}
	}
	
	return false;
}

FPCGPropertyAttributeWrapper PCGMetadataAttributeWrapper::CreateWrapper(UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	constexpr bool bIsReadOnly = false;

	FName Name = InSelector.GetName();

	if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
	{
		if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
		{
			if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				return FPCGPropertyAttributeWrapper(FPCGPoint::CreateCustomPropertyGetterSetter(Name), bIsReadOnly);
			}
			else if (FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				return FPCGPropertyAttributeWrapper(Property, bIsReadOnly);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGMetadataAttributeWrapper::CreateWrapper] Expected to select a property but the data doesn't support this property."));
		return FPCGPropertyAttributeWrapper();
	}

	UPCGMetadata* Metadata = nullptr;

	if (UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(InData))
	{
		Metadata = SpatialData->Metadata;
	}
	else if (UPCGParamData* ParamData = Cast<UPCGParamData>(InData))
	{
		Metadata = ParamData->Metadata;
	}

	if (!Metadata)
	{
		return FPCGPropertyAttributeWrapper();
	}

	FPCGMetadataAttributeBase* Attribute = Metadata->GetMutableAttribute(Name);

	if (Attribute)
	{
		return FPCGPropertyAttributeWrapper(Attribute, Metadata);
	}
	else
	{
		return FPCGPropertyAttributeWrapper();
	}
}

FPCGPropertyAttributeWrapper PCGMetadataAttributeWrapper::CreateWrapper(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	constexpr bool bIsReadOnly = true;

	FName Name = InSelector.GetName();

	if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
	{
		if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
		{
			if (FPCGPoint::HasCustomPropertyGetterSetter(Name))
			{
				return FPCGPropertyAttributeWrapper(FPCGPoint::CreateCustomPropertyGetterSetter(Name), bIsReadOnly);
			}
			else if (FProperty* Property = FPCGPoint::StaticStruct()->FindPropertyByName(Name))
			{
				return FPCGPropertyAttributeWrapper(Property, bIsReadOnly);
			}
		}
	}

	// At this point, it is not a point data or we didn't find a property.
	// We can't continue if it is a property wanted.
	if (InSelector.Selection == EPCGAttributePropertySelection::PointProperty)
	{
		UE_LOG(LogPCG, Error, TEXT("[PCGMetadataAttributeWrapper::CreateWrapper] Expected to select a property but the data doesn't support this property."));
		return FPCGPropertyAttributeWrapper();
	}

	const UPCGMetadata* Metadata = nullptr;

	if (const UPCGSpatialData* SpatialData = Cast<const UPCGSpatialData>(InData))
	{
		Metadata = SpatialData->Metadata;
	}
	else if (const UPCGParamData* ParamData = Cast<const UPCGParamData>(InData))
	{
		Metadata = ParamData->Metadata;
	}

	if (!Metadata)
	{
		return FPCGPropertyAttributeWrapper();
	}

	const FPCGMetadataAttributeBase* Attribute = Metadata->GetConstAttribute(Name);

	if (Attribute)
	{
		return FPCGPropertyAttributeWrapper(Attribute, Metadata);
	}
	else
	{
		return FPCGPropertyAttributeWrapper();
	}
}

FPCGPropertyAttributeIterator PCGMetadataAttributeWrapper::CreateIteratorWrapper(UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	FPCGPropertyAttributeWrapper Wrapper = PCGMetadataAttributeWrapper::CreateWrapper(InData, InSelector);

	if (!Wrapper.IsValid())
	{
		return FPCGPropertyAttributeIterator();
	}

	if (UPCGPointData* PointData = Cast<UPCGPointData>(InData))
	{
		TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
		return FPCGPropertyAttributeIterator(Wrapper, TArrayView<FPCGPoint>(Points.GetData(), Points.Num()));
	}
	else
	{
		return FPCGPropertyAttributeIterator(Wrapper);
	}
}

FPCGPropertyAttributeIterator PCGMetadataAttributeWrapper::CreateIteratorWrapper(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector)
{
	FPCGPropertyAttributeWrapper Wrapper = PCGMetadataAttributeWrapper::CreateWrapper(InData, InSelector);

	if (!Wrapper.IsValid())
	{
		return FPCGPropertyAttributeIterator();
	}

	if (const UPCGPointData* PointData = Cast<const UPCGPointData>(InData))
	{
		return FPCGPropertyAttributeIterator(Wrapper, PointData->GetPoints());
	}
	else
	{
		return FPCGPropertyAttributeIterator(Wrapper);
	}
}

FName FPCGAttributePropertySelector::GetName() const
{
	switch (Selection)
	{
	case EPCGAttributePropertySelection::PointProperty:
	{
		if (const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("/Script/PCG.EPCGPointProperties"), true))
		{
			// Need to use the string version and not the name version, because the name verison has "EPCGPointProperties::" as a prefix.
			return FName(EnumPtr->GetNameStringByValue((int64)PointProperty));
		}
		else
		{
			return NAME_None;
		}
	}
	default:
		return AttributeName;
	}
}