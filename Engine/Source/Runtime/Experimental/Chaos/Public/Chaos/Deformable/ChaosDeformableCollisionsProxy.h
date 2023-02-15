// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

namespace Chaos::Softs
{
	struct CHAOS_API FCollisionObjectAddedBodies
	{
		FCollisionObjectAddedBodies( const UObject* InBodyId = nullptr,
			FTransform InTransform = FTransform::Identity,
			FString InType = "",
			FImplicitObject* InShapes = nullptr)
			: BodyId(InBodyId)
			, Transform(InTransform)
			, Type(InType)
			, Shapes(InShapes) {}

	
		const UObject* BodyId = nullptr;
		FTransform Transform = FTransform::Identity;
		FString Type = "";
		FImplicitObject* Shapes = nullptr;
	};

	struct CHAOS_API FCollisionObjectRemovedBodies
	{
		const UObject* BodyId = nullptr;
	};

	struct CHAOS_API FCollisionObjectUpdatedBodies
	{
		const UObject* BodyId = nullptr;
		FTransform Transform = FTransform::Identity;
	};

	struct CHAOS_API FCollisionObjectParticleHandel
	{
		FCollisionObjectParticleHandel(int32 InParticleIndex = INDEX_NONE,
									   int32 InActiveViewIndex = INDEX_NONE,
									   FTransform InTransform = FTransform::Identity)
			: ParticleIndex(InParticleIndex)
			, ActiveViewIndex(InActiveViewIndex)
			, Transform(InTransform) {}

		int32 ParticleIndex = INDEX_NONE;
		int32 ActiveViewIndex = INDEX_NONE;
		FTransform Transform = FTransform::Identity;
	};


	class CHAOS_API FCollisionManagerProxy : public FThreadingProxy
	{
	public:
		typedef FThreadingProxy Super;

		FCollisionManagerProxy(UObject* InOwner)
			: Super(InOwner, TypeName())
		{}

		static FName TypeName() { return FName("CollisionManager"); }

		class FCollisionsInputBuffer : public FThreadingProxy::FBuffer
		{
			typedef FThreadingProxy::FBuffer Super;

		public:
			typedef FCollisionManagerProxy Source;

			FCollisionsInputBuffer(
				const TArray<FCollisionObjectAddedBodies>& InAdded
				, const TArray<FCollisionObjectRemovedBodies>& InRemoved
				, const TArray<FCollisionObjectUpdatedBodies>& InUpdate
				, const UObject* InOwner)
				: Super(InOwner, FCollisionManagerProxy::TypeName())
				, Added(InAdded)
				, Removed(InRemoved)
				, Updated(InUpdate)
			{}

			TArray<FCollisionObjectAddedBodies> Added;
			TArray<FCollisionObjectRemovedBodies> Removed;
			TArray<FCollisionObjectUpdatedBodies> Updated;
		};

		TArray<FCollisionObjectAddedBodies> CollisionObjectsToAdd;
		TArray< FCollisionObjectRemovedBodies > CollisionObjectsToRemove;
		TMap<const UObject*, FCollisionObjectParticleHandel > CollisionBodies;
	};

}// namespace Chaos::Softs