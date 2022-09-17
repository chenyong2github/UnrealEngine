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

	void FNearestNeighborEditorModelActor::InitNearestNeighborActor(UNearestNeighborModel* InNearestNeighborModel, const int32 InPartId)
	{
		NearestNeighborModel = InNearestNeighborModel;
		PartId = InPartId;
	}

	void FNearestNeighborEditorModelActor::TickNearestNeighborActor()
	{
		if (GeomCacheComponent && GeomCacheComponent->GetGeometryCache() && NearestNeighborModel)
		{
			GeomCacheComponent->SetManualTick(true);
			GeomCacheComponent->TickAtThisTime(GeomCacheComponent->GetTimeAtFrame(NearestNeighborModel->NearestNeighborId(PartId)), false, false, false);
		}
	}
}	// namespace UE::NearestNeighborModel
