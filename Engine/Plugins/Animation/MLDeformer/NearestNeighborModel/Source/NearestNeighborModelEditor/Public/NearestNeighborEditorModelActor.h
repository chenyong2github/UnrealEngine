// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerGeomCacheActor.h"
#include "NearestNeighborModelInstance.h"

class UMLDeformerComponent;
class UGeometryCacheComponent;

namespace UE::NearestNeighborModel
{
	using namespace UE::MLDeformer;

	enum : int32
	{
		ActorID_NearestNeighborActors = 6
	};

	class NEARESTNEIGHBORMODELEDITOR_API FNearestNeighborEditorModelActor
		: public FMLDeformerGeomCacheActor
	{
	public:
		FNearestNeighborEditorModelActor(const FConstructSettings& Settings);

		void SetGeometryCacheComponent(UGeometryCacheComponent* Component) { GeomCacheComponent = Component; }
		UGeometryCacheComponent* GetGeometryCacheComponent() const { return GeomCacheComponent; }

		void InitNearestNeighborActor(const int32 InPartId);
		void TickNearestNeighborActor();

		UNearestNeighborModelInstance* GetModelInstance() const;

	protected:
		int32 PartId = INDEX_NONE;
	};
}	// namespace UE::NearestNeighborModel
