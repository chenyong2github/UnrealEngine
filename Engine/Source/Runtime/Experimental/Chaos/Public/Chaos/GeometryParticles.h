// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/Rotation.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/CollisionFilterData.h"

#ifndef CHAOS_DETERMINISTIC
#define CHAOS_DETERMINISTIC 1
#endif

namespace Chaos
{
	/** Data that is associated with geometry. If a union is used an entry is created per internal geometry */
	template <typename T, int d>
	class TPerShapeData
	{
	public:
#if INCLUDE_CHAOS
		FCollisionFilterData QueryData;
		FCollisionFilterData SimData;
#endif
		void* UserData;
		const TImplicitObject<T, d>* Geometry;
		
		TPerShapeData()
			: UserData(nullptr)
		{
		}
	};

	template <typename T, int d>
	using TShapesArray = TArray<TUniquePtr<TPerShapeData<T, d>>, TInlineAllocator<1>>;

	template <typename T, int d>
	void UpdateShapesArrayFromGeometry(TShapesArray<T, d>& ShapesArray, const TImplicitObject<T, d>* Geometry);

	extern template void CHAOS_API UpdateShapesArrayFromGeometry(TShapesArray<float, 3>& ShapesArray, const TImplicitObject<float, 3>* Geometry);

	#if CHAOS_DETERMINISTIC
	using FParticleID = int32;	//Used to break ties when determinism is needed. Should not be used for anything else
#endif
	//Used for down casting when iterating over multiple SOAs.
	enum class EParticleType : uint8
	{
		Static,
		Kinematic,
		Dynamic,
		Clustered,	//only applicable on physics thread side
		StaticMesh,
		SkeletalMesh,
		GeometryCollection
	};

	enum class EGeometryParticlesSimType
	{
		RigidBodySim,
		Other
	};
	
	template<class T, int d, EGeometryParticlesSimType SimType>
	class TGeometryParticlesImp : public TParticles<T, d>
	{
	public:
		using TArrayCollection::Size;

		TGeometryParticlesImp()
		    : TParticles<T, d>()
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MParticleIDs);
			TArrayCollection::AddArray(&MShapesArray);

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}

		}
		TGeometryParticlesImp(const TGeometryParticlesImp<T, d, SimType>& Other) = delete;
		TGeometryParticlesImp(TGeometryParticlesImp<T, d, SimType>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
			, MR(MoveTemp(Other.MR))
			, MGeometry(MoveTemp(Other.MGeometry))
			, MSharedGeometry(MoveTemp(Other.MSharedGeometry))
			, MDynamicGeometry(MoveTemp(Other.MDynamicGeometry))
			, MGeometryParticleHandle(MoveTemp(Other.MGeometryParticleHandle))
			, MGeometryParticle(MoveTemp(Other.MGeometryParticle))
			, MShapesArray(MoveTemp(Other.MShapesArray))
#if CHAOS_DETERMINISTIC
			, MParticleIDs(MoveTemp(Other.MParticleIDs))
#endif
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MShapesArray);
#if CHAOS_DETERMINISTIC
			TArrayCollection::AddArray(&MParticleIDs);
#endif

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}
		}

		static constexpr bool IsRigidBodySim() { return SimType == EGeometryParticlesSimType::RigidBodySim; }

		TGeometryParticlesImp(TParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MShapesArray);
#if CHAOS_DETERMINISTIC
			TArrayCollection::AddArray(&MParticleIDs);
#endif

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}
		}

		const TRotation<T, d>& R(const int32 Index) const { return MR[Index]; }
		TRotation<T, d>& R(const int32 Index) { return MR[Index]; }

		TSerializablePtr<TImplicitObject<T, d>> Geometry(const int32 Index) const { return MGeometry[Index]; }

		const TUniquePtr<TImplicitObject<T, d>>& DynamicGeometry(const int32 Index) const { return MDynamicGeometry[Index]; }

		const TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe>& SharedGeometry(const int32 Index) const { return MSharedGeometry[Index]; }

		const TShapesArray<T, d>& ShapesArray(const int32 Index) const { return MShapesArray[Index]; }

#if CHAOS_DETERMINISTIC
		FParticleID ParticleID(const int32 Idx) const { return MParticleIDs[Idx]; }
		FParticleID& ParticleID(const int32 Idx) { return MParticleIDs[Idx]; }
#endif

		void SetDynamicGeometry(const int32 Index, TUniquePtr<TImplicitObject<T, d>>&& InUnique)
		{
			check(!SharedGeometry(Index));	// If shared geometry exists we should not be setting dynamic geometry on top
			MGeometry[Index] = MakeSerializable(InUnique);
			MDynamicGeometry[Index] = MoveTemp(InUnique);
			UpdateShapesArray(Index);
		}

		void SetSharedGeometry(const int32 Index, TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> InShared)
		{
			check(!DynamicGeometry(Index));	// If dynamic geometry exists we should not be setting shared geometry on top
			MGeometry[Index] = MakeSerializable(InShared);
			MSharedGeometry[Index] = InShared;
			UpdateShapesArray(Index);
		}
		
		void SetGeometry(const int32 Index, TSerializablePtr<TImplicitObject<T, d>> InGeometry)
		{
			check(!DynamicGeometry(Index));
			check(!SharedGeometry(Index));
			MGeometry[Index] = InGeometry;
			UpdateShapesArray(Index);
		}

		const TArray<TSerializablePtr<TImplicitObject<T, d>>>& GetAllGeometry() const { return MGeometry; }

		typedef TGeometryParticleHandle<T, d> THandleType;
		const THandleType* Handle(int32 Index) const { return MGeometryParticleHandle[Index]; }

		THandleType*& Handle(int32 Index){ return MGeometryParticleHandle[Index]; }

		TGeometryParticle<T, d>* GTGeometryParticle(const int32 Index) const { return MGeometryParticle[Index]; }
		TGeometryParticle<T, d>*& GTGeometryParticle(const int32 Index) { return MGeometryParticle[Index]; }

		FString ToString(int32 index) const
		{
			FString BaseString = TParticles<T, d>::ToString(index);
			return FString::Printf(TEXT("%s, MR:%s, MGeometry:%s, IsDynamic:%d"), *BaseString, *R(index).ToString(), (Geometry(index) ? *(Geometry(index)->ToString()) : TEXT("none")), (DynamicGeometry(index) != nullptr));
		}

		void Serialize(FChaosArchive& Ar)
		{
			TParticles<T, d>::Serialize(Ar);
			Ar << MGeometry << MDynamicGeometry << MR;
			//todo: serialize handles
		}

		EParticleType ParticleType() const { return MParticleType; }

	protected:
		EParticleType MParticleType;

	private:
		TArrayCollectionArray<TRotation<T, d>> MR;
		// MGeometry contains raw ptrs to every entry in both MSharedGeometry and MDynamicGeometry.
		// It may also contain raw ptrs to geometry which is managed outside of Chaos.
		TArrayCollectionArray<TSerializablePtr<TImplicitObject<T, d>>> MGeometry;
		// MSharedGeometry entries are owned by the solver, shared between *representations* of a particle.
		// This is NOT for sharing geometry resources between particle's A and B, this is for sharing the
		// geometry between particle A's various representations.
		TArrayCollectionArray<TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe>> MSharedGeometry;
		// MDynamicGeometry entries are used for geo which is by the evolution. It is not set from the game side.
		TArrayCollectionArray<TUniquePtr<TImplicitObject<T, d>>> MDynamicGeometry;
		TArrayCollectionArray<TGeometryParticleHandle<T, d>*> MGeometryParticleHandle;
		TArrayCollectionArray<TGeometryParticle<T, d>*> MGeometryParticle;
		TArrayCollectionArray<TShapesArray<T,d>> MShapesArray;

		void UpdateShapesArray(const int32 Index)
		{
			UpdateShapesArrayFromGeometry(MShapesArray[Index], MGeometry[Index].Get());
		}

#if CHAOS_DETERMINISTIC
		TArrayCollectionArray<FParticleID> MParticleIDs;
#endif
	};

	template <typename T, int d, EGeometryParticlesSimType SimType>
	FChaosArchive& operator<<(FChaosArchive& Ar, TGeometryParticlesImp<T, d, SimType>& Particles)
	{
		Particles.Serialize(Ar);
		return Ar;
	}

	template <typename T, int d>
	using TGeometryParticles = TGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>;

	template <typename T, int d>
	using TGeometryClothParticles = TGeometryParticlesImp<T, d, EGeometryParticlesSimType::Other>;
}
