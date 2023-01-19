// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDistance.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Data/PCGPointData.h"
#include "PCGContext.h"
#include "PCGPin.h"

namespace PCGDistance
{
	const FName SourceLabel = TEXT("Source");
	const FName TargetLabel = TEXT("Target");

	FVector CalcPosition(PCGDistanceShape Shape, const FPCGPoint& SourcePoint, const FPCGPoint& TargetPoint, const FVector SourceCenter, const FVector TargetCenter)
	{
		if (Shape == PCGDistanceShape::SphereBounds)
		{
			FVector Dir = TargetCenter - SourceCenter;
			Dir.Normalize();

			return SourceCenter + Dir * ((SourcePoint.BoundsMax-SourcePoint.BoundsMin) * SourcePoint.Transform.GetScale3D()).Length() * 0.5;
		}
		else if (Shape == PCGDistanceShape::BoxBounds)
		{
			const FVector LocalTargetCenter = SourcePoint.Transform.Inverse().TransformPosition(TargetCenter);

			const double DistanceSquared = ComputeSquaredDistanceFromBoxToPoint(SourcePoint.BoundsMin, SourcePoint.BoundsMax, LocalTargetCenter);

			FVector Dir = -LocalTargetCenter;
			Dir.Normalize();			

			const FVector LocalClosestPoint = LocalTargetCenter + Dir * FMath::Sqrt(DistanceSquared);

			return SourcePoint.Transform.TransformPosition(LocalClosestPoint);
		}

		// PCGDistanceShape::Center
		return SourceCenter;
	}
}
	
TArray<FPCGPinProperties> UPCGDistanceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGDistance::SourceLabel, EPCGDataType::Spatial);
	PinProperties.Emplace(PCGDistance::TargetLabel, EPCGDataType::Spatial);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGDistanceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Spatial);

	return PinProperties;
}

FPCGElementPtr UPCGDistanceSettings::CreateElement() const
{
	return MakeShared<FPCGDistanceElement>();
}

bool FPCGDistanceElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDistanceElement::Execute);

	const UPCGDistanceSettings* Settings = Context->GetInputSettings<UPCGDistanceSettings>();
	check(Settings);

	UPCGParamData* Params = Context->InputData.GetParams();

	const FName AttributeName = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDistanceSettings, AttributeName), Settings->AttributeName, Params);
	const bool bSetDensity = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDistanceSettings, bSetDensity), Settings->bSetDensity, Params);
	const double MaximumDistance = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDistanceSettings, MaximumDistance), Settings->MaximumDistance, Params);
	const PCGDistanceShape SourceShape = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDistanceSettings, SourceShape), Settings->SourceShape, Params);
	const PCGDistanceShape TargetShape = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDistanceSettings, TargetShape), Settings->TargetShape, Params);

	TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGDistance::SourceLabel);
	TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGDistance::TargetLabel);
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	FBox InputBounds(EForceInit::ForceInit);
	int32 InputPointCount = 0;

	TArray<const UPCGPointData*> TargetPointDatas;
	TargetPointDatas.Reserve(Targets.Num());

	for (const FPCGTaggedData& Target : Targets)
	{

		const UPCGSpatialData* TargetData = Cast<UPCGSpatialData>(Target.Data);

		if (!TargetData)
		{
			PCGE_LOG(Error, TEXT("Target must be spatial data, found '%s'"), *Target.Data->GetClass()->GetName());
			continue;
		}

		const UPCGPointData* TargetPointData = TargetData->ToPointData(Context);
		if (!TargetPointData)
		{
			PCGE_LOG(Error, TEXT("Cannot convert target '%s' into point data"), *Target.Data->GetClass()->GetName());
			continue;			
		}

		TargetPointDatas.Add(TargetPointData);
	}

	// first find the total Input bounds which will determine the size of each cell
	for (const FPCGTaggedData& Source : Sources) 
	{
		// add the point bounds to the input cell

		const UPCGSpatialData* SourceData = Cast<UPCGSpatialData>(Source.Data);

		if (!SourceData)
		{
			PCGE_LOG(Error, TEXT("Invalid input data"));
			continue;
		}

		const UPCGPointData* SourcePointData = SourceData->ToPointData(Context);
		if (!SourcePointData)
		{
			PCGE_LOG(Error, TEXT("Cannot convert input spatial data to point data"));
			continue;			
		}

		UPCGPointData* OutputData = NewObject<UPCGPointData>();
		OutputData->InitializeFromData(SourcePointData);
		Outputs.Add_GetRef(Source).Data = OutputData;

		FPCGMetadataAttribute<float>* Attribute = (AttributeName != NAME_None) ? OutputData->Metadata->FindOrCreateAttribute<float>(AttributeName, 0.0f) : nullptr;

		FPCGAsync::AsyncPointProcessing(Context, SourcePointData->GetPoints(), OutputData->GetMutablePoints(),
			[OutputData, SourceShape, TargetShape, &TargetPointDatas, MaximumDistance, Attribute, bSetDensity](const FPCGPoint& SourcePoint, FPCGPoint& OutPoint) {

				OutPoint = SourcePoint;

				const FBoxSphereBounds SourceQueryBounds = FBoxSphereBounds(FBox(SourcePoint.BoundsMin - FVector(MaximumDistance), SourcePoint.BoundsMax + FVector(MaximumDistance))).TransformBy(SourcePoint.Transform);

				const FVector SourceCenter = SourcePoint.Transform.TransformPosition(SourcePoint.GetLocalCenter());

				double DistanceSquared = MaximumDistance*MaximumDistance;

				auto CalculateSDF = [&DistanceSquared, &SourcePoint, SourceCenter, SourceShape, TargetShape](const FPCGPointRef& TargetPointRef)
				{
					const FPCGPoint& TargetPoint = *TargetPointRef.Point;

					const FVector TargetCenter = TargetPoint.Transform.TransformPosition(TargetPoint.GetLocalCenter());

					const FVector SourceShapePos = PCGDistance::CalcPosition(SourceShape, SourcePoint, TargetPoint, SourceCenter, TargetCenter);
					const FVector TargetShapePos = PCGDistance::CalcPosition(TargetShape, TargetPoint, SourcePoint, TargetCenter, SourceCenter);

					const FVector ToTargetShapeDir = TargetShapePos - SourceShapePos;
					const FVector ToTargetCenterDir = TargetCenter - SourceCenter;

					const double Sign = ToTargetShapeDir.Dot(ToTargetCenterDir) > 0 ? 1.0 : -1.0;
					const double ThisDistanceSquared = ToTargetShapeDir.SquaredLength() * Sign;

					DistanceSquared = FMath::Min(DistanceSquared, ThisDistanceSquared);
				};

				for (const UPCGPointData* TargetPointData : TargetPointDatas)
				{
					const UPCGPointData::PointOctree& Octree = TargetPointData->GetOctree();

					Octree.FindElementsWithBoundsTest(
						FBoxCenterAndExtent(SourceQueryBounds.Origin, SourceQueryBounds.BoxExtent),
						CalculateSDF
					);
				}

				const float Distance = FMath::Sqrt(FMath::Abs(DistanceSquared)) * (DistanceSquared < 0 ? -1.0 : 1.0);

				if (Attribute)
				{
					OutputData->Metadata->InitializeOnSet(OutPoint.MetadataEntry);
					Attribute->SetValue(OutPoint.MetadataEntry, Distance);
				}
				
				if (bSetDensity)
				{
					// set density instead
					OutPoint.Density = FMath::Clamp(Distance, -MaximumDistance, MaximumDistance) / MaximumDistance;
				}

				return true;
			}
		);		
	}

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetAllSettings());
	return true;

}
