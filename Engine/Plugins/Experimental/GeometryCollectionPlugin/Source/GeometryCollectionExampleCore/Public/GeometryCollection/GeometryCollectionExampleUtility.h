// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosSolversModule.h"
#include "Chaos/Serializable.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Templates/SharedPointer.h"

class FGeometryCollection;
class FGeometryDynamicCollection;

namespace Chaos
{
	class FPBDRigidsSolver;
}

namespace GeometryCollectionExample {

	TSharedPtr<FGeometryDynamicCollection> GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryCollectionToGeometryDynamicCollection(const FGeometryCollection* InputCollection
		, int DynamicStateDefault = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);

	void GEOMETRYCOLLECTIONEXAMPLECORE_API FinalizeSolver(Chaos::FPBDRigidsSolver& InSolver);

	TSharedPtr<FGeometryCollection>	CreateClusteredBody(FVector Position);
	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoParents_TwoBodies(FVector Position);
	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FourParents_OneBody(FVector Position);
	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoByTwo_ThreeTransform(FVector Position);
	TSharedPtr<FGeometryCollection>	CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector Position);
	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FracturedGeometry(FVector Position = FVector(0));


}
