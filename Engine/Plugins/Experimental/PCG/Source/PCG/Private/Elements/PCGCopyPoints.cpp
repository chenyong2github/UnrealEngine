// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGCopyPoints.h"

#include "PCGContext.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCopyPoints)

TArray<FPCGPinProperties> UPCGCopyPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGCopyPointsConstants::SourcePointsLabel, EPCGDataType::Point, /*bAllowMultipleConnections=*/false);
	PinProperties.Emplace(PCGCopyPointsConstants::TargetPointsLabel, EPCGDataType::Point, /*bAllowMultipleConnections=*/false);
	return PinProperties;
}

FPCGElementPtr UPCGCopyPointsSettings::CreateElement() const
{
	return MakeShared<FPCGCopyPointsElement>();
}

bool FPCGCopyPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCopyPointsElement::Execute);

	const UPCGCopyPointsSettings* Settings = Context->GetInputSettings<UPCGCopyPointsSettings>();
	check(Settings);

	const EPCGCopyPointsInheritanceMode RotationInheritance = Settings->RotationInheritance;
	const EPCGCopyPointsInheritanceMode ScaleInheritance = Settings->ScaleInheritance;
	const EPCGCopyPointsInheritanceMode ColorInheritance = Settings->ColorInheritance;
	const EPCGCopyPointsInheritanceMode SeedInheritance = Settings->SeedInheritance;
	const EPCGCopyPointsMetadataInheritanceMode AttributeInheritance = Settings->AttributeInheritance;

	const TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGCopyPointsConstants::SourcePointsLabel);
	const TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGCopyPointsConstants::TargetPointsLabel);

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	if (Sources.Num() != 1 || Targets.Num() != 1)
	{
		PCGE_LOG(Error, "Invalid number of inputs - Expected one source, got %d; Expected one target, got %d.", Sources.Num(), Targets.Num());
		return true;
	}
	
	const FPCGTaggedData& Source = Sources[0];
	const FPCGTaggedData& Target = Targets[0];

	FPCGTaggedData& Output = Outputs.Add_GetRef(Source);

	if (!Source.Data || !Target.Data) 
	{
		PCGE_LOG(Error, "Invalid input data");
		return true;
	}

	const UPCGSpatialData* SourceSpatialData = Cast<UPCGSpatialData>(Source.Data);
	const UPCGSpatialData* TargetSpatialData = Cast<UPCGSpatialData>(Target.Data);

	if (!SourceSpatialData || !TargetSpatialData)
	{
		PCGE_LOG(Error, "Unable to get SpatialData from input");
		return true;
	}

	const UPCGPointData* SourcePointData = SourceSpatialData->ToPointData(Context);
	const UPCGPointData* TargetPointData = TargetSpatialData->ToPointData(Context);

	if (!SourcePointData || !TargetPointData)
	{
		PCGE_LOG(Error, "Unable to get PointData from input");
		return true;
	}

	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();
	const TArray<FPCGPoint>& TargetPoints = TargetPointData->GetPoints();

	UPCGPointData* OutPointData = NewObject<UPCGPointData>();
	TArray<FPCGPoint>& OutPoints = OutPointData->GetMutablePoints();
	Output.Data = OutPointData;

	// RootMetadata will be parent to the ouptut metadata, while NonRootMetadata will carry attributes from the input not selected for inheritance
	const UPCGMetadata* RootMetadata = nullptr;
	const UPCGMetadata* NonRootMetadata = nullptr;
	if (Settings->AttributeInheritance == EPCGCopyPointsMetadataInheritanceMode::Source)
	{
		OutPointData->InitializeFromData(SourcePointData);
		RootMetadata = SourcePointData->Metadata;
		NonRootMetadata = TargetPointData->Metadata;
	}
	else // if (Settings->AttributeInheritance == EPCGCopyPointsMetadataInheritanceMode::Target)
	{
		OutPointData->InitializeFromData(TargetPointData);
		RootMetadata = TargetPointData->Metadata;
		NonRootMetadata = SourcePointData->Metadata;
	}

	// Priorize use the target actor from the target, irrespective of the source
	OutPointData->TargetActor = TargetPointData->TargetActor.IsValid() ? TargetPointData->TargetActor : SourcePointData->TargetActor;

	check(OutPointData->Metadata && NonRootMetadata);

	TArray<FName> AttributeNames;
	TArray<EPCGMetadataTypes> AttributeTypes;
	NonRootMetadata->GetAttributes(AttributeNames, AttributeTypes);

	TArray<FPCGMetadataAttributeBase*> NonRootAttributes;
	for (const FName& AttributeName : AttributeNames)
	{
		if (!OutPointData->Metadata->HasAttribute(AttributeName))
		{
			const FPCGMetadataAttributeBase* Attribute = NonRootMetadata->GetConstAttribute(AttributeName);
			if (FPCGMetadataAttributeBase* NewAttribute = OutPointData->Metadata->CopyAttribute(Attribute, AttributeName, /*bKeepRoot=*/false, /*bCopyEntries=*/false, /*bCopyValues=*/true))
			{
				NonRootAttributes.Add(NewAttribute);
			}
		}
	}

	TArray<TTuple<int64, int64>> AllMetadataEntries;
	AllMetadataEntries.SetNum(SourcePoints.Num() * TargetPoints.Num());

	// Use implicit capture, since we capture a lot
	FPCGAsync::AsyncPointProcessing(Context, SourcePoints.Num() * TargetPoints.Num(), OutPoints, [&](int32 Index, FPCGPoint& OutPoint)
	{
		const FPCGPoint& SourcePoint = SourcePoints[Index / TargetPoints.Num()];
		const FPCGPoint& TargetPoint = TargetPoints[Index % TargetPoints.Num()];

		OutPoint = SourcePoint;

		// Set Rotation, Scale, and Color based on inheritance mode
		if (RotationInheritance == EPCGCopyPointsInheritanceMode::Relative)
		{
			OutPoint.Transform.SetRotation(TargetPoint.Transform.GetRotation() * SourcePoint.Transform.GetRotation());
		}
		else if (RotationInheritance == EPCGCopyPointsInheritanceMode::Source)
		{
			OutPoint.Transform.SetRotation(SourcePoint.Transform.GetRotation());
		}
		else // if (RotationInheritance == EPCGCopyPointsInheritanceMode::Target)
		{
			OutPoint.Transform.SetRotation(TargetPoint.Transform.GetRotation());
		}

		if (ScaleInheritance == EPCGCopyPointsInheritanceMode::Relative)
		{
			OutPoint.Transform.SetScale3D(SourcePoint.Transform.GetScale3D() * TargetPoint.Transform.GetScale3D());
		}
		else if (ScaleInheritance == EPCGCopyPointsInheritanceMode::Source)
		{ 
			OutPoint.Transform.SetScale3D(SourcePoint.Transform.GetScale3D());
		}
		else // if (ScaleInheritance == EPCGCopyPointsInheritanceMode::Target)
		{
			OutPoint.Transform.SetScale3D(TargetPoint.Transform.GetScale3D());
		}

		if (ColorInheritance == EPCGCopyPointsInheritanceMode::Relative)
		{
			OutPoint.Color = SourcePoint.Color * TargetPoint.Color;
		}
		else if (ColorInheritance == EPCGCopyPointsInheritanceMode::Source)
		{ 
			OutPoint.Color = SourcePoint.Color;
		}
		else // if (ColorInheritance == EPCGCopyPointsInheritanceMode::Target)
		{ 
			OutPoint.Color = TargetPoint.Color;
		}

		const FVector Location = TargetPoint.Transform.TransformPosition(SourcePoint.Transform.GetLocation());
		OutPoint.Transform.SetLocation(Location);

		// Set seed based on inheritance mode
		if (SeedInheritance == EPCGCopyPointsInheritanceMode::Relative)
		{
			OutPoint.Seed = PCGHelpers::ComputeSeed(SourcePoint.Seed, TargetPoint.Seed);
		}
		else if (SeedInheritance == EPCGCopyPointsInheritanceMode::Target)
		{
			OutPoint.Seed = TargetPoint.Seed;
		}

		const FPCGPoint* RootPoint = nullptr;
		const FPCGPoint* NonRootPoint = nullptr;
		if (Settings->AttributeInheritance == EPCGCopyPointsMetadataInheritanceMode::Source)
		{
			RootPoint = &SourcePoint;
			NonRootPoint = &TargetPoint;
		}
		else // if (Settings->AttributeInheritance == EPCGCopyPointsMetadataInheritanceMode::Target)
		{
			RootPoint = &TargetPoint;
			NonRootPoint = &SourcePoint;
		}

		OutPoint.MetadataEntry = OutPointData->Metadata->AddEntryPlaceholder();
		AllMetadataEntries[Index] = TTuple<int64, int64>(OutPoint.MetadataEntry, RootPoint->MetadataEntry);

		// Copy EntryToValue key mappings from NonRootAttributes
		for (FPCGMetadataAttributeBase* NonRootAttribute : NonRootAttributes)
		{
			const FPCGMetadataAttributeBase* Attribute = NonRootMetadata->GetConstAttribute(NonRootAttribute->Name);
			check(Attribute);
			const PCGMetadataValueKey ValueKey = Attribute->GetValueKey(NonRootPoint->MetadataEntry);
			NonRootAttribute->SetValueFromValueKey(OutPoint.MetadataEntry, ValueKey);
		}

		return true;
	});

	OutPointData->Metadata->AddDelayedEntries(AllMetadataEntries);

	return true;
}
