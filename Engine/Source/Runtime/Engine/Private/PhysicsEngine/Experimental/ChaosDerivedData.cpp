// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosDerivedData.h"

#if WITH_CHAOS

#include "Chaos/CollisionConvexMesh.h"
#include "Chaos/ChaosArchive.h"
#include "ChaosDerivedDataUtil.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Convex.h"
#include "Serialization/Archive.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/MemoryWriter.h"

const TCHAR* FChaosDerivedDataCooker::GetPluginName() const
{
	return TEXT("ChaosGeometryData");
}

const TCHAR* FChaosDerivedDataCooker::GetVersionString() const
{
	return TEXT("19C69FC43DDA4F058B28C21F08D623F2");
}

FString FChaosDerivedDataCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString SetupGeometryKey(TEXT("INVALID"));

	if(Setup)
	{
		Setup->GetGeometryDDCKey(SetupGeometryKey);
	}

	return FString::Printf(TEXT("%s_%s"),
		*RequestedFormat.ToString(),
		*SetupGeometryKey);
}

bool FChaosDerivedDataCooker::IsBuildThreadsafe() const
{
	// #BG Investigate Parallel Build
	return false;
}

bool FChaosDerivedDataCooker::Build(TArray<uint8>& OutData)
{
	bool bSucceeded = false;

	if(Setup)
	{
		FCookBodySetupInfo CookInfo;

		EPhysXMeshCookFlags TempFlags = static_cast<EPhysXMeshCookFlags>(0); // #BGTODO Remove need for PhysX specific flags
		Setup->GetCookInfo(CookInfo, TempFlags);

		FMemoryWriter MemWriterAr(OutData);
		Chaos::FChaosArchive Ar(MemWriterAr);

		int32 PrecisionSize = (int32)sizeof(BuildPrecision);

		Ar << PrecisionSize;
		BuildInternal<BuildPrecision>(Ar, CookInfo);

		bSucceeded = true;
	}

	return bSucceeded;
}

void FChaosDerivedDataCooker::AddReferencedObjects(FReferenceCollector& Collector)
{
	if(Setup)
	{
		Collector.AddReferencedObject(Setup);
	}
}

FChaosDerivedDataCooker::FChaosDerivedDataCooker(UBodySetup* InSetup, FName InFormat)
	: Setup(InSetup)
	, RequestedFormat(InFormat)
{

}

template<typename Precision>
void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<Precision>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams)
{
	if(!InParams.bCookTriMesh)
	{
		return;
	}

	TArray<FVector> FinalVerts = InParams.TriangleMeshDesc.Vertices;

	// Push indices into one flat array
	TArray<int32> FinalIndices;
	FinalIndices.Reserve(InParams.TriangleMeshDesc.Indices.Num() * 3);
	for(const FTriIndices& Tri : InParams.TriangleMeshDesc.Indices)
	{
		FinalIndices.Add(Tri.v0);
		FinalIndices.Add(Tri.v1);
		FinalIndices.Add(Tri.v2);
	}

	Chaos::CleanTrimesh(FinalVerts, FinalIndices, nullptr);

	// Build particle list #BG Maybe allow TParticles to copy vectors?
	Chaos::TParticles<Precision, 3> TriMeshParticles;
	TriMeshParticles.AddParticles(FinalVerts.Num());

	const int32 NumVerts = FinalVerts.Num();
	for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
	{
		TriMeshParticles.X(VertIndex) = FinalVerts[VertIndex];
	}

	// Build chaos triangle list. #BGTODO Just make the clean function take these types instead of double copying
	const int32 NumTriangles = FinalIndices.Num() / 3;
	TArray<Chaos::TVector<int32, 3>> Triangles;
	Triangles.Reserve(NumTriangles);

	for(int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
	{
		const int32 BaseIndex = TriangleIndex * 3;
		Triangles.Add(Chaos::TVector<int32, 3>(FinalIndices[BaseIndex], FinalIndices[BaseIndex + 1], FinalIndices[BaseIndex + 2]));
	}

	OutTriangleMeshes.Emplace(new Chaos::TTriangleMeshImplicitObject<Precision>(MoveTemp(TriMeshParticles), MoveTemp(Triangles)));
}

template void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<float>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);
//#BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//template void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

template<typename Precision>
void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<TUniquePtr<Chaos::TImplicitObject<Precision, 3>>>& OutConvexMeshes, const FCookBodySetupInfo& InParams)
{
	auto BuildConvexFromVerts = [](TArray<TUniquePtr<Chaos::TImplicitObject<Precision, 3>>>& OutConvexes, const TArray<TArray<FVector>>& InMeshVerts)
	{
		for(const TArray<FVector>& HullVerts : InMeshVerts)
		{
			Chaos::TParticles<Precision, 3> ConvexParticles;

			const int32 NumHullVerts = HullVerts.Num();

			ConvexParticles.AddParticles(NumHullVerts);

			for(int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
			{
				ConvexParticles.X(VertIndex) = HullVerts[VertIndex];
			}

			OutConvexes.Emplace(new Chaos::TConvex<Precision, 3>(ConvexParticles));
		}
	};

	if(InParams.bCookNonMirroredConvex)
	{
		BuildConvexFromVerts(OutConvexMeshes, InParams.NonMirroredConvexVertices);
	}

	if(InParams.bCookMirroredConvex)
	{
		BuildConvexFromVerts(OutConvexMeshes, InParams.MirroredConvexVertices);
	}

}

template void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>>& OutConvexMeshes, const FCookBodySetupInfo& InParams);
//#BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//template void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<Chaos::TCollisionConvexMesh<double>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

template<typename Precision>
void BuildSimpleShapes(TArray<TUniquePtr<Chaos::TImplicitObject<Precision, 3>>>& OutImplicits, UBodySetup* InSetup)
{
	check(InSetup);

	FKAggregateGeom& AggGeom = InSetup->AggGeom;

	for(FKBoxElem& Box : AggGeom.BoxElems)
	{
		Chaos::TVector<Precision, 3> HalfBoxExtent = Chaos::TVector<Precision, 3>(Box.X, Box.Y, Box.Z) / 2.0f;
		TUniquePtr<Chaos::TBox<Precision, 3>> NonTransformed = MakeUnique<Chaos::TBox<Precision, 3>>(-HalfBoxExtent, HalfBoxExtent);
		Chaos::TRigidTransform<Precision, 3> ShapeLocalTransform(Box.Center, Box.Rotation.Quaternion());
		OutImplicits.Add(MakeUnique<Chaos::TImplicitObjectTransformed<Precision, 3>>(MakeSerializable(NonTransformed), MoveTemp(NonTransformed), ShapeLocalTransform));
	}

	for(FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		OutImplicits.Add(MakeUnique<Chaos::TSphere<Precision, 3>>(Sphere.Center, Sphere.Radius));
	}

	for(FKSphylElem& Sphyl : AggGeom.SphylElems)
	{
		const float HalfLength = Sphyl.Length / 2.0f;
		const Chaos::TVector<Precision, 3> TopPoint(0.0f, 0.0f, HalfLength);
		const Chaos::TVector<Precision, 3> BottomPoint(0.0f, 0.0f, -HalfLength);
		TUniquePtr<Chaos::TCapsule<Precision>> NonTransformed = MakeUnique<Chaos::TCapsule<Precision>>(TopPoint, BottomPoint, Sphyl.Radius);
		Chaos::TRigidTransform<Precision, 3> ShapeLocalTransform(Sphyl.Center, Sphyl.Rotation.Quaternion());
		OutImplicits.Add(MakeUnique<Chaos::TImplicitObjectTransformed<Precision, 3>>(MakeSerializable(NonTransformed), MoveTemp(NonTransformed), ShapeLocalTransform));
	}

	const int32 NumTaperedCapsules = AggGeom.TaperedCapsuleElems.Num();
	UE_CLOG(NumTaperedCapsules > 0, LogChaos, Warning, TEXT("Ignoring %d tapered spheres when building collision data for body setup %s"), NumTaperedCapsules, *InSetup->GetName());
}

template<typename Precision>
void FChaosDerivedDataCooker::BuildInternal(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo)
{
	TArray<TUniquePtr<Chaos::TImplicitObject<Precision, 3>>> SimpleImplicits;
	TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<Precision>>> ComplexImplicits;

	//BuildSimpleShapes(SimpleImplicits, Setup);
	BuildConvexMeshes(SimpleImplicits, InInfo);
	BuildTriangleMeshes(ComplexImplicits, InInfo);

	//TUniquePtr<Chaos::TImplicitObjectUnion<Precision, 3>> SimpleUnion = MakeUnique<Chaos::TImplicitObjectUnion<Precision, 3>>(MoveTemp(SimpleImplicits));
	//TUniquePtr<Chaos::TImplicitObjectUnion<Precision, 3>> ComplexUnion = MakeUnique<Chaos::TImplicitObjectUnion<Precision, 3>>(MoveTemp(ComplexImplicits));

	FBodySetupUVInfo UVInfo;
	if(InInfo.bSupportUVFromHitResults)
	{
		UVInfo.FillFromTriMesh(InInfo.TriangleMeshDesc);
	}

	Ar << SimpleImplicits << ComplexImplicits << UVInfo;
}

template void FChaosDerivedDataCooker::BuildInternal<float>(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo);
//#BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//template void FChaosDerivedDataCooker::BuildInternal<float>(FArchive& Ar, FCookBodySetupInfo& InInfo);

#endif
 
