// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborEditorModelActor.h"
#include "GeometryCacheComponent.h"

namespace UE::NearestNeighborModel
{
	using namespace UE::MLDeformer;

	FNearestNeighborEditorModelActor::FNearestNeighborEditorModelActor(const FConstructSettings& Settings)
		: FMLDeformerGeomCacheActor(Settings)
	{
	}

	void FNearestNeighborEditorModelActor::InitNearestNeighborActor(UNearestNeighborModelInstance* InModelInstance, const int32 InPartId)
	{
		ModelInstance = InModelInstance;
		PartId = InPartId;
	}

	void FNearestNeighborEditorModelActor::TickNearestNeighborActor()
	{
		if (GeomCacheComponent && GeomCacheComponent->GetGeometryCache() && ModelInstance && PartId < ModelInstance->NeighborIdNum())
		{
			GeomCacheComponent->SetManualTick(true);
			GeomCacheComponent->TickAtThisTime(GeomCacheComponent->GetTimeAtFrame(ModelInstance->NearestNeighborId(PartId)), false, false, false);
		}
	}
}	// namespace UE::NearestNeighborModel
