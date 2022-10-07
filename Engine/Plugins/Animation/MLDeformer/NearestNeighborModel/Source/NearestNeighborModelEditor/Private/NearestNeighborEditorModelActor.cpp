// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborEditorModelActor.h"
#include "MLDeformerComponent.h"
#include "GeometryCacheComponent.h"

namespace UE::NearestNeighborModel
{
	using namespace UE::MLDeformer;

	FNearestNeighborEditorModelActor::FNearestNeighborEditorModelActor(const FConstructSettings& Settings)
		: FMLDeformerGeomCacheActor(Settings)
	{
	}

	void FNearestNeighborEditorModelActor::InitNearestNeighborActor(const int32 InPartId)
	{
		PartId = InPartId;
	}

	void FNearestNeighborEditorModelActor::TickNearestNeighborActor()
	{
		const UNearestNeighborModelInstance* ModelInstance = GetModelInstance();
		if (GeomCacheComponent && GeomCacheComponent->GetGeometryCache() && ModelInstance && PartId < ModelInstance->NeighborIdNum())
		{
			GeomCacheComponent->SetManualTick(true);
			GeomCacheComponent->TickAtThisTime(GeomCacheComponent->GetTimeAtFrame(ModelInstance->NearestNeighborId(PartId)), false, false, false);
		}
	}

	UNearestNeighborModelInstance* FNearestNeighborEditorModelActor::GetModelInstance() const
	{
		UNearestNeighborModelInstance* ModelInstance = nullptr;
		const AActor* MyActor = GetActor();
		if (MyActor)
		{
			UMLDeformerComponent* Component = MyActor->FindComponentByClass<UMLDeformerComponent>();
			if (Component)
			{
				ModelInstance = static_cast<UNearestNeighborModelInstance*>(Component->GetModelInstance());
			}
		}
		return ModelInstance;
	}
}	// namespace UE::NearestNeighborModel
