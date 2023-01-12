// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancePackers/PCGInstancePackerByAttribute.h"
#include "Data/PCGSpatialData.h"
#include "InstancePackers/PCGInstancePackerBase.h"
#include "PCGContext.h"
#include "PCGElement.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGInstancePackerByAttribute)

void UPCGInstancePackerByAttribute::PackInstances_Implementation(FPCGContext& Context, const UPCGSpatialData* InSpatialData, const FPCGMeshInstanceList& InstanceList, FPCGPackedCustomData& OutPackedCustomData) const
{
	if (!InSpatialData || !InSpatialData->Metadata)
	{
		PCGE_LOG_C(Error, &Context, "Invalid input data");
		return;
	}

	TArray<const FPCGMetadataAttributeBase*> SelectedAttributes;

	// Find Attributes by name and calculate NumCustomDataFloats
	for (const FName& AttributeName : AttributeNames)
	{
		if (!InSpatialData->Metadata->HasAttribute(AttributeName)) 
		{
			PCGE_LOG_C(Warning, &Context, "Attribute %s is not in the metadata", *AttributeName.ToString());
			continue;
		}

		const FPCGMetadataAttributeBase* AttributeBase = InSpatialData->Metadata->GetConstAttribute(AttributeName);
		check(AttributeBase);

		if (!AddTypeToPacking(AttributeBase->GetTypeId(), OutPackedCustomData))
		{
			PCGE_LOG_C(Warning, &Context, "Attribute name %s is not a valid type", *AttributeName.ToString());
			continue;
		}

		SelectedAttributes.Add(AttributeBase);
	}

	PackCustomDataFromAttributes(InstanceList, SelectedAttributes, OutPackedCustomData);
 }

