// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGMetadataElement.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

FPCGElementPtr UPCGMetadataOperationSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataOperationElement>();
}

bool FPCGMetadataOperationElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataOperationElement::Execute);

	const UPCGMetadataOperationSettings* Settings = Context->GetInputSettings<UPCGMetadataOperationSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	for (const FPCGTaggedData& Input : Inputs)
	{
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Input.Data);

		if (!SpatialInput)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		const UPCGPointData* OriginalData = SpatialInput->ToPointData(Context);

		if (!OriginalData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		if (!OriginalData->Metadata)
		{
			PCGE_LOG(Warning, "Input has no metadata");
			continue;
		}

		// Check if the attribute exists
		if ((Settings->Target == EPCGMetadataOperationTarget::AttributeToProperty || Settings->Target == EPCGMetadataOperationTarget::AttributeToAttribute) && !OriginalData->Metadata->HasAttribute(Settings->SourceAttribute))
		{
			PCGE_LOG(Warning, "Input does not have the %s attribute", *Settings->SourceAttribute.ToString());
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		const int OriginalPointCount = Points.Num();

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->InitializeFromData(OriginalData);
		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();

		Output.Data = SampledData;

		// Copy points and then apply the operation
		SampledPoints = Points;

		if (Settings->Target == EPCGMetadataOperationTarget::PropertyToAttribute)
		{
			if (Settings->PointProperty == EPCGPointProperties::Density)
			{
				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateFloatAttribute(Settings->DestinationAttribute, 0.0f, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<float>::Id)
				{
					FPCGMetadataAttribute<float>* Attribute = static_cast<FPCGMetadataAttribute<float>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Attribute->SetValue(Point.MetadataEntry, Point.Density);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Extents)
			{
				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(Settings->DestinationAttribute, FVector::Zero() , /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
				{
					FPCGMetadataAttribute<FVector>* Attribute = static_cast<FPCGMetadataAttribute<FVector>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Attribute->SetValue(Point.MetadataEntry, Point.Extents);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if(Settings->PointProperty == EPCGPointProperties::Color)
			{
				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVector4Attribute(Settings->DestinationAttribute, FVector4::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
				{
					FPCGMetadataAttribute<FVector4>* Attribute = static_cast<FPCGMetadataAttribute<FVector4>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Attribute->SetValue(Point.MetadataEntry, Point.Color);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
		}
		else if(Settings->Target == EPCGMetadataOperationTarget::AttributeToProperty) // Attribute to property
		{
			if (Settings->PointProperty == EPCGPointProperties::Density)
			{
				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<float>::Id)
				{
					const FPCGMetadataAttribute<float>* Attribute = static_cast<const FPCGMetadataAttribute<float>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Point.Density = Attribute->GetValueFromItemKey(Point.MetadataEntry);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Extents)
			{
				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
				{
					const FPCGMetadataAttribute<FVector>* Attribute = static_cast<const FPCGMetadataAttribute<FVector>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Point.Extents = Attribute->GetValueFromItemKey(Point.MetadataEntry);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Color)
			{
				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
				{
					const FPCGMetadataAttribute<FVector4>* Attribute = static_cast<const FPCGMetadataAttribute<FVector4>*>(AttributeBase);
					for (FPCGPoint& Point : SampledPoints)
					{
						Point.Color = Attribute->GetValueFromItemKey(Point.MetadataEntry);
					}
				}
				else
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
		}
		else // Attribute to attribute
		{
			SampledData->Metadata->CopyAttribute(Settings->SourceAttribute, Settings->DestinationAttribute);
		}
	}

	return true;
}