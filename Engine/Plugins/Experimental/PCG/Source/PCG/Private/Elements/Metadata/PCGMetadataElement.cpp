// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataElement.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGMetadataOperations
{
	template<typename U, typename T>
	bool SetValue(TArray<FPCGPoint>& InPoints, FPCGMetadataAttributeBase* AttributeBase, UPCGMetadata* Metadata, TFunctionRef<U(const FPCGPoint& InPoint)> PropGetter)
	{
		if (AttributeBase && AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
		{
			FPCGMetadataAttribute<T>* Attribute = static_cast<FPCGMetadataAttribute<T>*>(AttributeBase);
			for (FPCGPoint& Point : InPoints)
			{
				Metadata->InitializeOnSet(Point.MetadataEntry);
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
	UPCGParamData* Params = Context->InputData.GetParams();

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const FName SourceAttribute = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataOperationSettings, SourceAttribute), Settings->SourceAttribute, Params);
	const EPCGPointProperties PointProperty = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataOperationSettings, PointProperty), Settings->PointProperty, Params);
	const FName DestinationAttribute = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataOperationSettings, DestinationAttribute), Settings->DestinationAttribute, Params);
	const EPCGMetadataOperationTarget Target = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataOperationSettings, Target), Settings->Target, Params);

	// Forward any non-input data
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

		const FName LocalSourceAttribute = ((SourceAttribute != NAME_None) ? SourceAttribute : OriginalData->Metadata->GetLatestAttributeNameOrNone());

		// Check if the attribute exists
		if ((Target == EPCGMetadataOperationTarget::AttributeToProperty || Target == EPCGMetadataOperationTarget::AttributeToAttribute) && !OriginalData->Metadata->HasAttribute(LocalSourceAttribute))
		{
			PCGE_LOG(Warning, "Input does not have the %s attribute", *LocalSourceAttribute.ToString());
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

		if (Target == EPCGMetadataOperationTarget::PropertyToAttribute)
		{
			if (PointProperty == EPCGPointProperties::Density)
			{
				auto DensityGetter = [](const FPCGPoint& InPoint) { return InPoint.Density; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateFloatAttribute(DestinationAttribute, 0.0f, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<float, float>(SampledPoints, AttributeBase, SampledData->Metadata, DensityGetter) &&
					!PCGMetadataOperations::SetValue<float, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, DensityGetter) &&
					!PCGMetadataOperations::SetValue<float, FVector4>(SampledPoints, AttributeBase, SampledData->Metadata, DensityGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::BoundsMin)
			{
				auto ExtentsGetter = [](const FPCGPoint& InPoint) { return InPoint.BoundsMin; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(DestinationAttribute, FVector::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::BoundsMax)
			{
				auto ExtentsGetter = [](const FPCGPoint& InPoint) { return InPoint.BoundsMax; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(DestinationAttribute, FVector::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Extents)
			{
				auto ExtentsGetter = [](const FPCGPoint& InPoint) { return InPoint.GetExtents(); };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(DestinationAttribute, FVector::Zero() , /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, SampledData->Metadata, ExtentsGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if(PointProperty == EPCGPointProperties::Color)
			{
				auto ColorGetter = [](const FPCGPoint& InPoint) { return InPoint.Color; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVector4Attribute(DestinationAttribute, FVector4::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if(!PCGMetadataOperations::SetValue<FVector4, FVector4>(SampledPoints, AttributeBase, SampledData->Metadata, ColorGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Position)
			{
				auto PositionGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetLocation(); };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(DestinationAttribute, FVector::Zero(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, PositionGetter) &&
					!PCGMetadataOperations::SetValue<FVector, FTransform>(SampledPoints, AttributeBase, SampledData->Metadata, PositionGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Rotation)
			{
				auto RotationGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetRotation(); };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateQuatAttribute(DestinationAttribute, FQuat::Identity, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FQuat, FQuat>(SampledPoints, AttributeBase, SampledData->Metadata, RotationGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Scale)
			{
				auto ScaleGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform.GetScale3D(); };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateVectorAttribute(DestinationAttribute, FVector::One(), /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FVector>(SampledPoints, AttributeBase, SampledData->Metadata, ScaleGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Transform)
			{
				auto TransformGetter = [](const FPCGPoint& InPoint) { return InPoint.Transform; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateTransformAttribute(DestinationAttribute, FTransform::Identity, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<FTransform, FTransform>(SampledPoints, AttributeBase, SampledData->Metadata, TransformGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Steepness)
			{
				auto SteepnessGetter = [](const FPCGPoint& InPoint) { return InPoint.Steepness; };

				if (!SampledData->Metadata->HasAttribute(DestinationAttribute))
				{
					SampledData->Metadata->CreateFloatAttribute(DestinationAttribute, 0.5f, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);
				}

				FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetMutableAttribute(DestinationAttribute);
				if (!PCGMetadataOperations::SetValue<float, float>(SampledPoints, AttributeBase, SampledData->Metadata, SteepnessGetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *DestinationAttribute.ToString());
				}
			}
		}
		else if(Target == EPCGMetadataOperationTarget::AttributeToProperty) // Attribute to property
		{
			if (PointProperty == EPCGPointProperties::Density)
			{
				auto DensitySetter = [](FPCGPoint& InPoint, const float& InValue) { InPoint.Density = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, float>(AttributeBase, SampledPoints, DensitySetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::BoundsMin)
			{
				auto ExtentsSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.BoundsMin = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ExtentsSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ExtentsSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::BoundsMax)
			{
				auto ExtentsSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.BoundsMax = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ExtentsSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ExtentsSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Extents)
			{
				auto ExtentsSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.SetExtents(InValue); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ExtentsSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ExtentsSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Color)
			{
				auto ColorSetter = [](FPCGPoint& InPoint, const FVector4& InValue) { InPoint.Color = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if(!PCGMetadataOperations::SetValue<float, FVector4>(AttributeBase, SampledPoints, ColorSetter) &&
					!PCGMetadataOperations::SetValue<FVector4, FVector4>(AttributeBase, SampledPoints, ColorSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Position)
			{
				auto PositionSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.Transform.SetLocation(InValue); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, PositionSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, PositionSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Rotation)
			{
				auto RotationSetter = [](FPCGPoint& InPoint, const FQuat& InValue) { InPoint.Transform.SetRotation(InValue.GetNormalized()); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<FQuat, FQuat>(AttributeBase, SampledPoints, RotationSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Scale)
			{
				auto ScaleSetter = [](FPCGPoint& InPoint, const FVector& InValue) { InPoint.Transform.SetScale3D(InValue); };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, FVector>(AttributeBase, SampledPoints, ScaleSetter) &&
					!PCGMetadataOperations::SetValue<FVector, FVector>(AttributeBase, SampledPoints, ScaleSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Transform)
			{
				auto TransformSetter = [](FPCGPoint& InPoint, const FTransform& InValue) { InPoint.Transform = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<FVector, FTransform>(AttributeBase, SampledPoints, TransformSetter) &&
					!PCGMetadataOperations::SetValue<FTransform, FTransform>(AttributeBase, SampledPoints, TransformSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
			else if (PointProperty == EPCGPointProperties::Steepness)
			{
				auto SteepnessSetter = [](FPCGPoint& InPoint, const float& InValue) { InPoint.Steepness = InValue; };

				const FPCGMetadataAttributeBase* AttributeBase = SampledData->Metadata->GetConstAttribute(LocalSourceAttribute);
				if (!PCGMetadataOperations::SetValue<float, float>(AttributeBase, SampledPoints, SteepnessSetter))
				{
					PCGE_LOG(Error, "Attribute %s already exists but its type is not compatible", *LocalSourceAttribute.ToString());
				}
			}
		}
		else // Attribute to attribute
		{
			SampledData->Metadata->CopyExistingAttribute(LocalSourceAttribute, DestinationAttribute);
		}
	}

	return true;
}