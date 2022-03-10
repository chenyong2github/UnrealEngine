// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDebugElement.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGActorHelpers.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace PCGDebugElement
{
	void ExecuteDebugDisplay(FPCGContext* Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGDebugElement::ExecuteDebugDisplay);
#if WITH_EDITORONLY_DATA
		const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();

		if (!Settings)
		{
			return;
		}

		const FPCGDebugVisualizationSettings& DebugSettings = Settings->DebugSettings;

		UStaticMesh* Mesh = DebugSettings.PointMesh.LoadSynchronous();

		if (!Mesh)
		{
			return;
		}

		UMaterialInterface* Material = DebugSettings.GetMaterial().LoadSynchronous();

		TArray<UMaterialInterface*> Materials;
		if (Material)
		{
			Materials.Add(Material);
		}

		TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();

		for(const FPCGTaggedData& Input : Inputs)
		{
			const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

			if (!SpatialData)
			{
				// Data type mismatch
				continue;
			}

			AActor* TargetActor = SpatialData->TargetActor;

			if (!TargetActor)
			{
				// No target actor
				continue;
			}

			const UPCGPointData* PointData = SpatialData->ToPointData(Context);

			if (!PointData)
			{
				continue;
			}

			const TArray<FPCGPoint>& Points = PointData->GetPoints();

			if (Points.Num() == 0)
			{
				continue;
			}

			const int NumCustomData = 8;

			TArray<FTransform> Instances;
			TArray<float> InstanceCustomData;

			Instances.Reserve(Points.Num());
			InstanceCustomData.Reserve(NumCustomData);

			// First, create target instance transforms
			const float PointScale = DebugSettings.PointScale;
			const bool bIsRelative = DebugSettings.ScaleMethod == EPCGDebugVisScaleMethod::Relative;

			for (const FPCGPoint& Point : Points)
			{
				FTransform& InstanceTransform = Instances.Add_GetRef(Point.Transform);
				InstanceTransform.SetScale3D(bIsRelative ? InstanceTransform.GetScale3D() * PointScale : FVector(PointScale));
			}

			UInstancedStaticMeshComponent* ISMC = UPCGActorHelpers::GetOrCreateISMC(TargetActor, Context->SourceComponent, Mesh, Materials);
			
			ISMC->ComponentTags.AddUnique(PCGHelpers::DefaultPCGDebugTag);
			ISMC->NumCustomDataFloats = NumCustomData;
			const int32 PreExistingInstanceCount = ISMC->GetInstanceCount();
			ISMC->AddInstances(Instances, false);

			// Then get & assign custom data
			for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
			{
				const FPCGPoint& Point = Points[PointIndex];
				InstanceCustomData.Add(Point.Density);
				InstanceCustomData.Add(Point.Extents[0]);
				InstanceCustomData.Add(Point.Extents[1]);
				InstanceCustomData.Add(Point.Extents[2]);
				InstanceCustomData.Add(Point.Color[0]);
				InstanceCustomData.Add(Point.Color[1]);
				InstanceCustomData.Add(Point.Color[2]);
				InstanceCustomData.Add(Point.Color[3]);

				ISMC->SetCustomData(PointIndex + PreExistingInstanceCount, InstanceCustomData);

				InstanceCustomData.Reset();
			}

			ISMC->UpdateBounds();
		}
#endif
	}
}

FPCGElementPtr UPCGDebugSettings::CreateElement() const
{
	return MakeShared<FPCGDebugElement>();
}

bool FPCGDebugElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDebugElement::Execute);
	PCGDebugElement::ExecuteDebugDisplay(Context);
	
	Context->OutputData = Context->InputData;
	return true;
}