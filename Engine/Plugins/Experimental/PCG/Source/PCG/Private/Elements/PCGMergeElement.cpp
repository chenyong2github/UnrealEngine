// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGMergeElement.h"

#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMergeElement)

#define LOCTEXT_NAMESPACE "PCGMergeElement"

#if WITH_EDITOR
FText UPCGMergeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("MergeNodeTooltip", "Merges multiple data sources into a single data output.");
}
#endif

TArray<FPCGPinProperties> UPCGMergeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, /*bAllowMultipleConnections=*/true);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGMergeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, /*bAllowMultipleConnections=*/false);

	return PinProperties;
}

FPCGElementPtr UPCGMergeSettings::CreateElement() const
{
	return MakeShared<FPCGMergeElement>();
}

bool FPCGMergeElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMergeElement::Execute);
	check(Context);

	const UPCGMergeSettings* Settings = Context->GetInputSettings<UPCGMergeSettings>();
	check(Settings);

	const bool bMergeMetadata = Settings->bMergeMetadata;

	TArray<FPCGTaggedData> Sources = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	if (Sources.IsEmpty())
	{
		return true;
	}

	UPCGPointData* TargetPointData = nullptr;
	FPCGTaggedData* TargetTaggedData = nullptr;

	// Prepare data & metadata
	// Done in two passes for futureproofing - expecting changes in the metadata attribute creation vs. usage in points
	for (const FPCGTaggedData& Source : Sources)
	{
		const UPCGPointData* SourcePointData = Cast<const UPCGPointData>(Source.Data);

		if (!SourcePointData)
		{
			PCGE_LOG(Error, "Unsupported data type in merge");
			continue;
		}

		if (!TargetPointData)
		{
			TargetPointData = NewObject<UPCGPointData>();
			TargetPointData->InitializeFromData(SourcePointData, nullptr, bMergeMetadata);

			TargetTaggedData = &(Outputs.Emplace_GetRef(Source));
			TargetTaggedData->Data = TargetPointData;
		}
		else
		{
			if (bMergeMetadata)
			{
				TargetPointData->Metadata->AddAttributes(SourcePointData->Metadata);
			}
			
			check(TargetTaggedData);
			TargetTaggedData->Tags.Append(Source.Tags); // TODO: only unique? if yes, fix union too
		}
	}

	// No valid input types
	if (!TargetPointData)
	{
		return true;
	}

	bool bIsFirst = true;
	TArray<FPCGPoint>& TargetPoints = TargetPointData->GetMutablePoints();
	
	for (FPCGTaggedData& Source : Sources)
	{
		const UPCGPointData* SourcePointData = Cast<const UPCGPointData>(Source.Data);

		if (!SourcePointData)
		{
			continue;
		}

		if (bIsFirst)
		{
			check(!bMergeMetadata || TargetPointData->Metadata->GetParent() == SourcePointData->Metadata);
			TargetPoints.Append(SourcePointData->GetPoints());
			bIsFirst = false;
		}
		else
		{
			int32 PointOffset = TargetPoints.Num();
			TargetPoints.Append(SourcePointData->GetPoints());

			if (bMergeMetadata && TargetPointData->Metadata && SourcePointData->Metadata && !SourcePointData->GetPoints().IsEmpty())
			{
				TargetPointData->Metadata->SetPointAttributes(MakeArrayView(SourcePointData->GetPoints()), SourcePointData->Metadata, MakeArrayView(&TargetPoints[PointOffset], SourcePointData->GetPoints().Num()));
			}
			// TBD: should we null out the metadata entry keys on the vertices if we didn't setup the metadata?
		}
	}
	
	return true;
}

#undef LOCTEXT_NAMESPACE