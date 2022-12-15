// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSurfaceSampler.h"

#include "PCGComponent.h"
#include "PCGCustomVersion.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

#include "Math/RandomStream.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSurfaceSampler)

#define LOCTEXT_NAMESPACE "PCGSurfaceSamplerElement"

namespace PCGSurfaceSamplerConstants
{
	const FName SurfaceLabel = TEXT("Surface");
	const FName BoundingShapeLabel = TEXT("Bounding Shape");
}

namespace PCGSurfaceSampler
{
	bool FSurfaceSamplerSettings::Initialize(const UPCGSurfaceSamplerSettings* InSettings, FPCGContext* Context, const FBox& InputBounds)
	{
		Settings = InSettings;

		if (Settings)
		{
			UPCGParamData* Params = Context ? Context->InputData.GetParams() : nullptr;
			
			// Compute used values
			PointsPerSquaredMeter = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, PointsPerSquaredMeter), Settings->PointsPerSquaredMeter, Params);
			PointExtents = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, PointExtents), Settings->PointExtents, Params);
			Looseness = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, Looseness), Settings->Looseness, Params);
			bApplyDensityToPoints = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, bApplyDensityToPoints), Settings->bApplyDensityToPoints, Params);
			PointSteepness = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, PointSteepness), Settings->PointSteepness, Params);
#if WITH_EDITORONLY_DATA
			bKeepZeroDensityPoints = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, bKeepZeroDensityPoints), Settings->bKeepZeroDensityPoints, Params);
#endif

			Seed = PCGSettingsHelpers::ComputeSeedWithOverride(InSettings, Context ? Context->SourceComponent : nullptr, Params);
		}
		else
		{
			Seed = (Context && Context->SourceComponent.IsValid()) ? Context->SourceComponent->Seed : 42;
		}

		// Conceptually, we will break down the surface bounds in a N x M grid
		InterstitialDistance = PointExtents * 2;
		InnerCellSize = InterstitialDistance * Looseness;
		CellSize = InterstitialDistance + InnerCellSize;
		check(CellSize.X > 0 && CellSize.Y > 0);

		// By using scaled indices in the world, we can easily make this process deterministic
		CellMinX = FMath::CeilToInt((InputBounds.Min.X) / CellSize.X);
		CellMaxX = FMath::FloorToInt((InputBounds.Max.X) / CellSize.X);
		CellMinY = FMath::CeilToInt((InputBounds.Min.Y) / CellSize.Y);
		CellMaxY = FMath::FloorToInt((InputBounds.Max.Y) / CellSize.Y);

		if (CellMinX > CellMaxX || CellMinY > CellMaxY)
		{
			if (Context)
			{
				PCGE_LOG_C(Verbose, Context, "Skipped - invalid cell bounds");
			}
			
			return false;
		}

		CellCount = (1 + CellMaxX - CellMinX) * (1 + CellMaxY - CellMinY);
		check(CellCount > 0);

		const FVector::FReal InvSquaredMeterUnits = 1.0 / (100.0 * 100.0);
		TargetPointCount = (InputBounds.Max.X - InputBounds.Min.X) * (InputBounds.Max.Y - InputBounds.Min.Y) * PointsPerSquaredMeter * InvSquaredMeterUnits;

		if (TargetPointCount == 0)
		{
			if (Context)
			{
				PCGE_LOG_C(Verbose, Context, "Skipped - density yields no points");
			}
			
			return false;
		}
		else if (TargetPointCount > CellCount)
		{
			TargetPointCount = CellCount;
		}

		Ratio = TargetPointCount / (FVector::FReal)CellCount;

		InputBoundsMaxZ = InputBounds.Max.Z;

		return true;
	}

	FIntVector2 FSurfaceSamplerSettings::ComputeCellIndices(int32 Index) const
	{
		check(Index >= 0 && Index < CellCount);
		const int32 CellCountX = 1 + CellMaxX - CellMinX;

		return FIntVector2(CellMinX + (Index % CellCountX), CellMinY + (Index / CellCountX));
	}

	UPCGPointData* SampleSurface(FPCGContext* Context, const UPCGSpatialData* InSurface, const UPCGSpatialData* InBoundingShape, const FSurfaceSamplerSettings& LoopData)
	{
		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->InitializeFromData(InSurface);

		SampleSurface(Context, InSurface, InBoundingShape, LoopData, SampledData);

		return SampledData;
	}

	void SampleSurface(FPCGContext* Context, const UPCGSpatialData* InSurface, const UPCGSpatialData* InBoundingShape, const FSurfaceSamplerSettings& LoopData, UPCGPointData* SampledData)
	{
		check(InSurface);

		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();

		FPCGProjectionParams ProjectionParams{};

		FPCGAsync::AsyncPointProcessing(Context, LoopData.CellCount, SampledPoints, [&LoopData, SampledData, InBoundingShape, InSurface, &ProjectionParams](int32 Index, FPCGPoint& OutPoint)
		{
			const FIntVector2 Indices = LoopData.ComputeCellIndices(Index);

			const FVector::FReal CurrentX = Indices.X * LoopData.CellSize.X;
			const FVector::FReal CurrentY = Indices.Y * LoopData.CellSize.Y;
			const FVector InnerCellSize = LoopData.InnerCellSize;

			FRandomStream RandomSource(PCGHelpers::ComputeSeed(LoopData.Seed, Indices.X, Indices.Y));
			float Chance = RandomSource.FRand();

			const float Ratio = LoopData.Ratio;

			if (Chance >= Ratio)
			{
				return false;
			}

			const float RandX = RandomSource.FRand();
			const float RandY = RandomSource.FRand();
			
			const FVector TentativeLocation = FVector(CurrentX + RandX * InnerCellSize.X, CurrentY + RandY * InnerCellSize.Y, LoopData.InputBoundsMaxZ);
			const FBox LocalBound(-LoopData.PointExtents, LoopData.PointExtents);

			// Firstly project onto elected generating shape to move to final position.
			if (!InSurface->ProjectPoint(FTransform(TentativeLocation), LocalBound, ProjectionParams, OutPoint, SampledData->Metadata))
			{
				return false;
			}

			// Now run gauntlet of shape network (if there is one) to accept or reject the point.
			if (InBoundingShape)
			{
				FPCGPoint BoundingShapeSample;
#if WITH_EDITORONLY_DATA
				if (!InBoundingShape->SamplePoint(OutPoint.Transform, OutPoint.GetLocalBounds(), BoundingShapeSample, nullptr) && !LoopData.bKeepZeroDensityPoints)
#else
				if (!InBoundingShape->SamplePoint(OutPoint.Transform, OutPoint.GetLocalBounds(), BoundingShapeSample, nullptr))
#endif
				{
					return false;
				}

				// Produce smooth density field
				OutPoint.Density *= BoundingShapeSample.Density;
			}

			// Apply final parameters on the point
			OutPoint.SetExtents(LoopData.PointExtents);
			OutPoint.Density *= (LoopData.bApplyDensityToPoints ? ((Ratio - Chance) / Ratio) : 1.0f);
			OutPoint.Steepness = LoopData.PointSteepness;
			OutPoint.Seed = RandomSource.GetCurrentSeed();

			return true;
		});

		if (Context)
		{
			PCGE_LOG_C(Verbose, Context, "Generated %d points in %d cells", SampledPoints.Num(), LoopData.CellCount);
		}
	}
}

UPCGSurfaceSamplerSettings::UPCGSurfaceSamplerSettings()
{
	bUseSeed = true;
}

#if WITH_EDITOR
FText UPCGSurfaceSamplerSettings::GetNodeTooltipText() const
{
	return LOCTEXT("SurfaceSamplerNodeTooltip", "Generates points in two dimensional domain that sample the Surface input and lie within the Bounding Shape input.");
}
#endif

TArray<FPCGPinProperties> UPCGSurfaceSamplerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGSurfaceSamplerConstants::SurfaceLabel, EPCGDataType::Surface, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/false, LOCTEXT("SurfaceSamplerSurfacePinTooltip",
		"The surface to sample with points. Points will be generated in the two dimensional footprint of the combined bounds of the Surface and the Bounding Shape (if any) "
		"and then projected onto this surface. If this input is omitted then the network of shapes connected to the Bounding Shape pin will be inspected for a surface "
		"shape to use to project the points onto."
	));
	// Only one connection allowed, user can union multiple shapes
	PinProperties.Emplace(PCGSurfaceSamplerConstants::BoundingShapeLabel, EPCGDataType::Spatial, /*bInAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false, LOCTEXT("SurfaceSamplerBoundingShapePinTooltip",
		"All sampled points must be contained within this shape. If this input is omitted then bounds will be taken from the actor so that points are contained within actor bounds. "
		"The Unbounded property disables this and instead generates over the entire bounds of Surface."
	));

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGSurfaceSamplerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);

	return PinProperties;
}

void UPCGSurfaceSamplerSettings::PostLoad()
{
	Super::PostLoad();

	if (PointRadius_DEPRECATED != 0)
	{
		PointExtents = FVector(PointRadius_DEPRECATED);
		PointRadius_DEPRECATED = 0;
	}
}

#if WITH_EDITOR
bool UPCGSurfaceSamplerSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	return !bUnbounded || InPin->Properties.Label != PCGSurfaceSamplerConstants::BoundingShapeLabel;
}
#endif

FPCGElementPtr UPCGSurfaceSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGSurfaceSamplerElement>();
}

bool FPCGSurfaceSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSurfaceSamplerElement::Execute);
	// TODO: time-sliced implementation
	check(Context);
	const UPCGSurfaceSamplerSettings* Settings = Context->GetInputSettings<UPCGSurfaceSamplerSettings>();
	check(Settings);
	
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Grab the Bounding Shape input if there is one.
	TArray<FPCGTaggedData> BoundingShapeInputs = Context->InputData.GetInputsByPin(PCGSurfaceSamplerConstants::BoundingShapeLabel);
	const UPCGSpatialData* BoundingShapeSpatialInput = nullptr;
	if (!Settings->bUnbounded)
	{
		if (BoundingShapeInputs.Num() > 0)
		{
			ensure(BoundingShapeInputs.Num() == 1);
			BoundingShapeSpatialInput = Cast<UPCGSpatialData>(BoundingShapeInputs[0].Data);
		}
		else if (Context->SourceComponent.IsValid())
		{
			// Fallback to getting bounds from actor
			BoundingShapeSpatialInput = Cast<UPCGSpatialData>(Context->SourceComponent->GetActorPCGData());
		}
	}
	else if (BoundingShapeInputs.Num() > 0)
	{
		PCGE_LOG(Verbose, "The bounds of the Bounding Shape input pin will be ignored because the Unbounded option is enabled.");
	}

	FBox BoundingShapeBounds(EForceInit::ForceInit);
	if (BoundingShapeSpatialInput)
	{
		BoundingShapeBounds = BoundingShapeSpatialInput->GetBounds();
	}

	TArray<FPCGTaggedData> SurfaceInputs = Context->InputData.GetInputsByPin(PCGSurfaceSamplerConstants::SurfaceLabel);

	// Construct a list of shapes to generate samples from. Prefer to get these directly from the first input pin.
	TArray<const UPCGSpatialData*, TInlineAllocator<16>> GeneratingShapes;
	for (FPCGTaggedData& TaggedData : SurfaceInputs)
	{
		if (UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
		{
			GeneratingShapes.Add(SpatialData);
			Outputs.Add(TaggedData);
		}
	}

	// If no shapes were obtained from the first input pin, try to find a shape to sample from nodes connected to the second pin.
	const UPCGSpatialData* GeneratorFromBoundingShapeInput = nullptr;
	if (GeneratingShapes.Num() == 0 && BoundingShapeSpatialInput)
	{
		GeneratorFromBoundingShapeInput = BoundingShapeSpatialInput->FindShapeFromNetwork(/*InDimension=*/2);
		if (GeneratorFromBoundingShapeInput)
		{
			GeneratingShapes.Add(GeneratorFromBoundingShapeInput);

			check(BoundingShapeInputs.Num() == 1);
			Outputs.Add(BoundingShapeInputs[0]);
		}
	}

	UPCGParamData* Params = Context->InputData.GetParams();

	// Early out on invalid settings
	// TODO: we could compute an approximate radius based on the points per squared meters if that's useful
	const FVector PointExtents = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSurfaceSamplerSettings, PointExtents), Settings->PointExtents, Params);
	if(PointExtents.X <= 0 || PointExtents.Y <= 0)
	{
		PCGE_LOG(Warning, "Skipped - Invalid point extents");
		return true;
	}
	
	// TODO: embarassingly parallel loop
	for (int GenerationIndex = 0; GenerationIndex < GeneratingShapes.Num(); ++GenerationIndex)
	{
		// If we have generating shape inputs, use them
		const UPCGSpatialData* GeneratingShape = GeneratingShapes[GenerationIndex];
		check(GeneratingShape);

		// Calculate the intersection of bounds of the provided inputs
		FBox InputBounds = GeneratingShape->GetBounds();
		if (BoundingShapeBounds.IsValid)
		{
			InputBounds = PCGHelpers::OverlapBounds(InputBounds, BoundingShapeBounds);
		}
		if (!InputBounds.IsValid)
		{
			PCGE_LOG(Warning, "Input data has invalid bounds");
			continue;
		}

		PCGSurfaceSampler::FSurfaceSamplerSettings LoopData;
		if (!LoopData.Initialize(Settings, Context, InputBounds))
		{
			continue;
		}

		// Sample surface
		Outputs[GenerationIndex].Data = PCGSurfaceSampler::SampleSurface(Context, GeneratingShape, BoundingShapeSpatialInput, LoopData);
	}

	// Finally, forward any exclusions/settings
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}

#if WITH_EDITOR
void UPCGSurfaceSamplerSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	if (DataVersion < FPCGCustomVersion::SplitSamplerNodesInputs)
	{
		// Deprecation from a single pin node.
		check(InputPins.Num() == 1);

		// In prior versions this node had a single "In" pin. In later versions this is split. The node will function
		// the same if we move all connections from "In" to "Bounding Shape". To make this happen, rename "In" to
		// "Bounding Shape" just prior to pin update and the edges will be moved over.
		InputPins[0]->Properties.Label = PCGSurfaceSamplerConstants::BoundingShapeLabel;
	}

	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
