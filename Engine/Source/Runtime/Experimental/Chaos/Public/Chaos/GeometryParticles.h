// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/Rotation.h"

namespace Chaos
{
	template <typename T, int d>
	class TGeometryParticleHandle;

	template<class T, int d>
	class TGeometryParticles : public TParticles<T, d>
	{
	public:
		using TArrayCollection::Size;

		TGeometryParticles()
		    : TParticles<T, d>()
		{
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MGeometryParticleHandle);
		}
		TGeometryParticles(const TGeometryParticles<T, d>& Other) = delete;
		TGeometryParticles(TGeometryParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other)), MR(MoveTemp(Other.MR)), MGeometry(MoveTemp(Other.MGeometry)), MDynamicGeometry(MoveTemp(Other.MDynamicGeometry))
		{
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MGeometryParticleHandle);
		}
		TGeometryParticles(TParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
		{
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MGeometryParticleHandle);
		}
			
		/**
		 * Constructor that replaces the positions array with a view of @param Points.
		 */
		TGeometryParticles(TArray<TVector<T, d>>& Points)
		    : TParticles<T, d>(Points)
		{
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MGeometryParticleHandle);
		}
		
		const TRotation<T, d>& R(const int32 Index) const { return MR[Index]; }
		TRotation<T, d>& R(const int32 Index) { return MR[Index]; }

		TSerializablePtr<TImplicitObject<T, d>> Geometry(const int32 Index) const { return MGeometry[Index]; }

		const TUniquePtr<TImplicitObject<T, d>>& DynamicGeometry(const int32 Index) const { return MDynamicGeometry[Index]; }

		void SetDynamicGeometry(const int32 Index, TUniquePtr<TImplicitObject<T, d>>&& Unique)
		{
			MDynamicGeometry[Index] = MoveTemp(Unique);
			MGeometry[Index] = MakeSerializable(DynamicGeometry(Index));
		}

		void SetGeometry(const int32 Index, TSerializablePtr<TImplicitObject<T, d>> Serializable)
		{
			check(!DynamicGeometry(Index));	//If dynamic geometry exists we should not be setting other geometry on top
			MGeometry[Index] = Serializable;
		}

		const TArray<TSerializablePtr<TImplicitObject<T, d>>>& GetAllGeometry() const { return MGeometry; }

		typedef TGeometryParticleHandle<T, d> THandleType;
		const THandleType* Handle(int32 Index) const { return MGeometryParticleHandle[Index]; }

		THandleType*& Handle(int32 Index){ return MGeometryParticleHandle[Index]; }

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

	private:
		TArrayCollectionArray<TRotation<T, d>> MR;
		TArrayCollectionArray<TSerializablePtr<TImplicitObject<T, d>>> MGeometry;
		TArrayCollectionArray<TUniquePtr<TImplicitObject<T, d>>> MDynamicGeometry;
		TArrayCollectionArray<TGeometryParticleHandle<T, d>*> MGeometryParticleHandle;
	};

	template <typename T, int d>
	FChaosArchive& operator<<(FChaosArchive& Ar, TGeometryParticles<T, d>& Particles)
	{
		Particles.Serialize(Ar);
		return Ar;
	}
}
