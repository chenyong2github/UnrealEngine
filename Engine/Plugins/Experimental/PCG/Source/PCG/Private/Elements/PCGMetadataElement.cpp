// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGMetadataElement.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGMetadataOperations
{
	template<typename U, typename T>
	bool SetValue(const TArray<FPCGPoint>& InPoints, FPCGMetadataAttributeBase* AttributeBase, TFunctionRef<U(const FPCGPoint& InPoint)> PropGetter)
	{
		if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
		{
			FPCGMetadataAttribute<T>* Attribute = static_cast<FPCGMetadataAttribute<T>*>(AttributeBase);
			for (const FPCGPoint& Point : InPoints)
			{
				Attribute->SetValue(Point.MetadataEntry, PropGetter(Point));
			}

			return true;
		}
		else
		{
			return false;
		}
	}

	template<typename T, typename U>
	bool SetValue(const FPCGMetadataAttributeBase* AttributeBase, TArray<FPCGPoint>& InPoints, TFunctionRef<void(FPCGPoint& OutPoint, const U& InValue)> PropSetter)
	{
		if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
		{
			const FPCGMetadataAttribute<T>* Attribute = static_cast<const FPCGMetadataAttribute<T>*>(AttributeBase);
			for (FPCGPoint& Point : InPoints)
			{
				PropSetter(Point, U(Attribute->GetValueFromItemKey(Point.MetadataEntry)));
			}

			return true;
		}
		else
		{
			return false;
		}
	}
}

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
				auto DensityGetter = [](const FPCGPoint& InPoint) { return InPoint.Density; };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateFloatAttribute(Settings->DestinationAttribute, 0.0f, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<float, float>(SampledPoints, AttributeBase, DensityGetter) &&
					!PCGMetadataOperations::SetValue<float, FVector>(SampledPoints, AttributeBase, DensityGetter) &&
					!PCGMetadataOperations::SetValue<float, FVector4>(SampledPoints, AttributeBase, DensityGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Extents)
			{
				auto ExtentsGetter = [](const FPCGPoint& InPoint) { return InPoint.Extents; };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(Settings->DestinationAttribute, FVector::Zero() , /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, ExtentsGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, ExtentsGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if(Settings->PointProperty == EPCGPointProperties::Color)
			{
				auto ColorGetter = [](const FPCGPoint& InPoint) { return InPoint.Color; };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVector4Attribute(Settings->DestinationAttribute, FVector4::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<FVector4, FVector4>(SampledPoints, AttributeBase, ColorGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Position)
			{
				auto PositionGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetLocation(); };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(Settings->DestinationAttribute, FVector::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, PositionGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, PositionGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Rotation)
			{
				auto RotationGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetRotation(); };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateQuatAttribute(Settings->DestinationAttribute, FQuat::Identity, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FQuat, FQuat>(SampledPoints, AttributeBase, RotationGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Scale)
			{
				auto ScaleGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetScale3D(); };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(Settings->DestinationAttribute, FVector::One(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, ScaleGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Transform)
			{
				auto TransformGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform; };

				if (!SampledData->Metadata->HasAttribute(Settings->DestinationAttribute))
				{
					SampledData->Metadata->CreateTransformAttribute(Settings->DestinationAttribute, FTransform::Identity, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(Settings->DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FTransform, FTransform>(SampledPoints, AttributeBase, TransformGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->DestinationAttribute.ToString());
				}
			}
		}
		else if(Settings->Target == EPCGMetadataOperationTarget::AttributeToProperty) // Attribute to property
		{
			if (Settings->PointProperty == EPCGPointProperties::Density)
			{
				auto DensitySetter = [](FPCGPoint& InPoint, const float& InValue) { InPoint.Density = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, float>(AttributeBase, SampledPoints, DensitySetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Extents)
			{
				auto ExtentsSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.Extents = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ExtentsSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ExtentsSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Color)
			{
				auto ColorSetter = [](FPCGPoint& InPoint, const FVector4& InValue) { InPoint.Color = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, FVector4>(AttributeBase, SampledPoints, ColorSetter) &&
					!PCGMetadataOperations::SetValue<FVector4, FVector4>(AttributeBase, SampledPoints, ColorSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Position)
			{
				auto PositionSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.Transform.SetLocation(InValue); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, PositionSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, PositionSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Rotation)
			{
				auto RotationSetter = [](FPCGPoint& InPoint, const FQuat& InValue) { InPoint.Transform.SetRotation(InValue.GetNormalized()); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (!PCGMetadataOperations::SetValue<FQuat, FQuat>(AttributeBase, SampledPoints, RotationSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Scale)
			{
				auto ScaleSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.Transform.SetScale3D(InValue); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ScaleSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ScaleSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *Settings->SourceAttribute.ToString());
				}
			}
			else if (Settings->PointProperty == EPCGPointProperties::Transform)
			{
				auto TransformSetter = [](FPCGPoint& InPoint, const FTransform& InValue) { InPoint.Transform = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(Settings->SourceAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FTransform>(AttributeBase, SampledPoints, TransformSetter) &&
					!PCGMetadataOperations::SetValue<FTransform, FTransform>(AttributeBase, SampledPoints, TransformSetter))
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