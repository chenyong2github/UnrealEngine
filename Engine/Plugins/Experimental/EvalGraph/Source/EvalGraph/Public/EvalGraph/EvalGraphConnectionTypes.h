// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "GeometryCollection/ManagedArrayCollection.h"

#include "Math/Box.h"
#include "Math/Box2D.h"
#include "Math/Color.h"
#include "Math/IntVector.h"
#include "Math/Matrix.h"
#include "Math/TransformCalculus2D.h"
#include "Math/OrientedBox.h"
#include "Math/OrthoMatrix.h"
#include "Math/Plane.h"
#include "Math/Quat.h"
#include "Math/TransformCalculus2D.h"
#include "Math/QuatRotationTranslationMatrix.h"
#include "Math/Sphere.h"
#include "Math/TransformNonVectorized.h"
#include "Math/TransformCalculus2D.h"
#include "Math/TranslationMatrix.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/RotationMatrix.h"

#include "Chaos/ImplicitObject.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Convex.h"


namespace Eg
{
	typedef TSharedPtr<FManagedArrayCollection, ESPMode::ThreadSafe> FManagedArrayCollectionSharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<double>, ESPMode::ThreadSafe> FManagedDoubleArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<float>, ESPMode::ThreadSafe> FManagedFloatArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<int32>, ESPMode::ThreadSafe> FManagedInt32ArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<bool>, ESPMode::ThreadSafe> FManagedBoolArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FVector3d>, ESPMode::ThreadSafe> FManagedVector3dArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FVector3f>, ESPMode::ThreadSafe> FManagedVector3fArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FTransform>, ESPMode::ThreadSafe> FManagedTransformArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FIntVector>, ESPMode::ThreadSafe> FManagedIntVectorArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FQuat4f>, ESPMode::ThreadSafe> FManagedQuat4fArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FVector2f>, ESPMode::ThreadSafe> FManagedVector2fArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FLinearColor>, ESPMode::ThreadSafe> FManagedLinearColorArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FString>, ESPMode::ThreadSafe> FManagedStringArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FBox>, ESPMode::ThreadSafe> FManagedBoxArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<TSet<int32>>, ESPMode::ThreadSafe> FManagedIntSetArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<FIntVector4>, ESPMode::ThreadSafe> FManagedIntVector4ArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<TArray<FVector2f>>, ESPMode::ThreadSafe> FManagedVector2fAArraySharedSafePtr;
	typedef TSharedPtr<TManagedArrayBase<TArray<FVector3f>*>, ESPMode::ThreadSafe> FManagedVector3fAPtrArraySharedSafePtr;


	typedef Chaos::TSerializablePtr<Chaos::FImplicitObject3> FImplicitObject3SerializablePtr;
	typedef TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> FImplicitObjectSharedSafePtr;

	// ---------------------------------------------------------
	//
	// General purpose EManagedArrayType definition. 
	// This defines things like 
	//     EGraphConnectionType::FManagedArrayCollectionType
	// see EvalGraphConnectionTypeValues.inl for specific types.
	//
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) F##A##Type,
	enum class EGraphConnectionType : uint32
	{
		FNoneType,
#include "EvalGraphConnectionTypeValues.inl"
	};
#undef EVAL_GRAPH_CONNECTION_TYPE

	// ---------------------------------------------------------
	//  GraphConnectionType<T>
	//    Templated function to return a EGraphConnectionType.
	//
	template<class T> inline EGraphConnectionType GraphConnectionType();
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template<> inline EGraphConnectionType GraphConnectionType<a>() { return EGraphConnectionType::F##A##Type; }
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE

	// ---------------------------------------------------------
	//  GraphConnectionTypeName
	//    Templated function to return a EGraphConnectionType.
	//
	template<class T> inline FName GraphConnectionTypeName();
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template<> inline FName GraphConnectionTypeName<a>() { return FName(TEXT(#A)); }
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE

// ---------------------------------------------------------
//  GraphConnectionTypeName
//    Templated function to return a EGraphConnectionType as a FName
//
	inline FName GraphConnectionTypeName(EGraphConnectionType ValueType)
	{
		switch (ValueType)
		{
#define EVAL_GRAPH_CONNECTION_TYPE(a,A)	case EGraphConnectionType::F##A##Type:\
		return FName(TEXT(#A));
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE
		}
		return FName("FNoneType");
	}

	// ---------------------------------------------------------
	//  void*
	//     Returns a new EGraphConnectionType pointer based on 
	//     passed type.
	//
	inline void* NewGraphValueType(EGraphConnectionType ValueType)
	{
		switch (ValueType)
		{
#define EVAL_GRAPH_CONNECTION_TYPE(a,A)	case EGraphConnectionType::F##A##Type:\
		return new a();
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE
		}
		check(false);
		return nullptr;
	}

}

