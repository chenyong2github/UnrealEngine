// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatchAndSet/PCGMatchAndSetBase.h"

#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGPointMatchAndSet.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMatchAndSetBase)

void UPCGMatchAndSetBase::SetType(EPCGMetadataTypes InType, EPCGMetadataTypesConstantStructStringMode InStringMode)
{
	Type = InType;
	StringMode = InStringMode;
}

bool UPCGMatchAndSetBase::CreateAttributeIfNeeded(FPCGContext& Context, const FPCGAttributePropertySelector& Selector, const FPCGMetadataTypesConstantStruct& ConstantValue, UPCGPointData* OutPointData, const UPCGPointMatchAndSetSettings* InSettings) const
{
	check(OutPointData && OutPointData->Metadata);

	check(OutPointData->Metadata);
	if (Selector.Selection == EPCGAttributePropertySelection::Attribute)
	{
		FName DestinationAttribute = Selector.GetName();
		if (DestinationAttribute == NAME_None)
		{
			DestinationAttribute = OutPointData->Metadata->GetLatestAttributeNameOrNone();
		}

		if (!OutPointData->Metadata->HasAttribute(DestinationAttribute) ||
			OutPointData->Metadata->GetConstAttribute(DestinationAttribute)->GetTypeId() != static_cast<uint16>(InSettings->SetTargetType))
		{
			auto CreateAttribute = [OutPointData, &DestinationAttribute](auto&& Value)
			{
				using ConstantType = std::decay_t<decltype(Value)>;
				return PCGMetadataElementCommon::ClearOrCreateAttribute(OutPointData->Metadata, DestinationAttribute, ConstantType{}) != nullptr;
			};

			if (!ConstantValue.Dispatcher(CreateAttribute))
			{
				PCGE_LOG_C(Error, &Context, "Unable to create attribute %s on point data", *DestinationAttribute.ToString());
				return false;
			}
		}
	}

	return true;
}
