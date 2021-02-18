// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEngine/Experimental/ChaosCooking.h"
#include "ChaosDerivedDataUtil.h"
#include "Chaos/Particles.h"
#include "Chaos/AABB.h"
#include "Chaos/Convex.h"
#include "Chaos/TriangleMeshImplicitObject.h"

#if WITH_CHAOS

int32 EnableMeshClean = 1;
FAutoConsoleVariableRef CVarEnableMeshClean(TEXT("p.EnableMeshClean"), EnableMeshClean, TEXT("Enable/Disable mesh cleanup during cook."));

namespace Chaos
{
	namespace Cooking
	{
		TUniquePtr<Chaos::FTriangleMeshImplicitObject> BuildSingleTrimesh(const FTriMeshCollisionData& Desc, TArray<int32>& OutFaceRemap, TArray<int32>& OutVertexRemap)
		{
			if(Desc.Vertices.Num() == 0)
			{
				return nullptr;
			}

			TArray<FVector> FinalVerts = Desc.Vertices;

			// Push indices into one flat array
			TArray<int32> FinalIndices;
			FinalIndices.Reserve(Desc.Indices.Num() * 3);
			for(const FTriIndices& Tri : Desc.Indices)
			{
				//question: It seems like unreal triangles are CW, but couldn't find confirmation for this
				FinalIndices.Add(Tri.v1);
				FinalIndices.Add(Tri.v0);
				FinalIndices.Add(Tri.v2);
			}

			if(EnableMeshClean)
			{
				Chaos::CleanTrimesh(FinalVerts, FinalIndices, &OutFaceRemap, &OutVertexRemap);
			}

			// Build particle list #BG Maybe allow TParticles to copy vectors?
			Chaos::TParticles<Chaos::FReal, 3> TriMeshParticles;
			TriMeshParticles.AddParticles(FinalVerts.Num());

			const int32 NumVerts = FinalVerts.Num();
			for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
			{
				TriMeshParticles.X(VertIndex) = FinalVerts[VertIndex];
			}

			// Build chaos triangle list. #BGTODO Just make the clean function take these types instead of double copying
			auto LambdaHelper = [&Desc, &FinalVerts, &FinalIndices, &TriMeshParticles, &OutFaceRemap, &OutVertexRemap](auto& Triangles) -> TUniquePtr<Chaos::FTriangleMeshImplicitObject>
			{
				const int32 NumTriangles = FinalIndices.Num() / 3;
				bool bHasMaterials = Desc.MaterialIndices.Num() > 0;
				TArray<uint16> MaterialIndices;

				if(bHasMaterials)
				{
					MaterialIndices.Reserve(NumTriangles);
				}

				Triangles.Reserve(NumTriangles);
				for(int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
				{
					// Only add this triangle if it is valid
					const int32 BaseIndex = TriangleIndex * 3;
					const bool bIsValidTriangle = Chaos::FConvexBuilder::IsValidTriangle(
						FinalVerts[FinalIndices[BaseIndex]],
						FinalVerts[FinalIndices[BaseIndex + 1]],
						FinalVerts[FinalIndices[BaseIndex + 2]]);

					// TODO: Figure out a proper way to handle this. Could these edges get sewn together? Is this important?
					//if (ensureMsgf(bIsValidTriangle, TEXT("FChaosDerivedDataCooker::BuildTriangleMeshes(): Trimesh attempted cooked with invalid triangle!")));
					if(bIsValidTriangle)
					{
						Triangles.Add(Chaos::TVector<int32, 3>(FinalIndices[BaseIndex], FinalIndices[BaseIndex + 1], FinalIndices[BaseIndex + 2]));

						if(bHasMaterials)
						{
							if(EnableMeshClean)
							{
								if(!ensure(OutFaceRemap.IsValidIndex(TriangleIndex)))
								{
									MaterialIndices.Empty();
									bHasMaterials = false;
								}
								else
								{
									const int32 OriginalIndex = OutFaceRemap[TriangleIndex];

									if(ensure(Desc.MaterialIndices.IsValidIndex(OriginalIndex)))
									{
										MaterialIndices.Add(Desc.MaterialIndices[OriginalIndex]);
									}
									else
									{
										MaterialIndices.Empty();
										bHasMaterials = false;
									}
								}
							}
							else
							{
								if(ensure(Desc.MaterialIndices.IsValidIndex(TriangleIndex)))
								{
									MaterialIndices.Add(Desc.MaterialIndices[TriangleIndex]);
								}
								else
								{
									MaterialIndices.Empty();
									bHasMaterials = false;
								}
							}

						}
					}
				}

				TUniquePtr<TArray<int32>> OutFaceRemapPtr = MakeUnique<TArray<int32>>(OutFaceRemap);
				TUniquePtr<TArray<int32>> OutVertexRemapPtr = Chaos::TriMeshPerPolySupport ? MakeUnique<TArray<int32>>(OutVertexRemap) : nullptr;
				return MakeUnique<Chaos::FTriangleMeshImplicitObject>(MoveTemp(TriMeshParticles), MoveTemp(Triangles), MoveTemp(MaterialIndices), MoveTemp(OutFaceRemapPtr), MoveTemp(OutVertexRemapPtr));
			};

			if(FinalVerts.Num() < TNumericLimits<uint16>::Max())
			{
				TArray<Chaos::TVector<uint16, 3>> TrianglesSmallIdx;
				return LambdaHelper(TrianglesSmallIdx);
			}
			else
			{
				TArray<Chaos::TVector<int32, 3>> TrianglesLargeIdx;
				return LambdaHelper(TrianglesLargeIdx);
			}

			return nullptr;
		}

		void BuildConvexMeshes(TArray<TUniquePtr<Chaos::FImplicitObject>>& OutConvexMeshes, const FCookBodySetupInfo& InParams)
		{
			using namespace Chaos;
			auto BuildConvexFromVerts = [](TArray<TUniquePtr<Chaos::FImplicitObject>>& OutConvexes, const TArray<TArray<FVector>>& InMeshVerts, const bool bMirrored)
			{
				for(const TArray<FVector>& HullVerts : InMeshVerts)
				{
					const int32 NumHullVerts = HullVerts.Num();
					if(NumHullVerts == 0)
					{
						continue;
					}

					// Calculate the margin to apply to the convex - it depends on overall dimensions
					FAABB3 Bounds = FAABB3::EmptyAABB();
					for(int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
					{
						const FVector& HullVert = HullVerts[VertIndex];
						Bounds.GrowToInclude(HullVert);
					}

					// Create the corner vertices for the convex
					TArray<FVec3> ConvexVertices;
					ConvexVertices.SetNumZeroed(NumHullVerts);

					for(int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
					{
						const FVector& HullVert = HullVerts[VertIndex];
						ConvexVertices[VertIndex] = FVector(bMirrored ? -HullVert.X : HullVert.X, HullVert.Y, HullVert.Z);
					}

					// Margin is always zero on convex shapes - they are intended to be instanced
					OutConvexes.Emplace(new Chaos::FConvex(ConvexVertices, 0.0f));
				}
			};

			if(InParams.bCookNonMirroredConvex)
			{
				BuildConvexFromVerts(OutConvexMeshes, InParams.NonMirroredConvexVertices, false);
			}

			if(InParams.bCookMirroredConvex)
			{
				BuildConvexFromVerts(OutConvexMeshes, InParams.MirroredConvexVertices, true);
			}
		}

		void BuildTriangleMeshes(TArray<TUniquePtr<Chaos::FTriangleMeshImplicitObject>>& OutTriangleMeshes, TArray<int32>& OutFaceRemap, TArray<int32>& OutVertexRemap, const FCookBodySetupInfo& InParams)
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
				//question: It seems like unreal triangles are CW, but couldn't find confirmation for this
				FinalIndices.Add(Tri.v1);
				FinalIndices.Add(Tri.v0);
				FinalIndices.Add(Tri.v2);
			}

			if(EnableMeshClean)
			{
				Chaos::CleanTrimesh(FinalVerts, FinalIndices, &OutFaceRemap, &OutVertexRemap);
			}

			// Build particle list #BG Maybe allow TParticles to copy vectors?
			Chaos::TParticles<Chaos::FReal, 3> TriMeshParticles;
			TriMeshParticles.AddParticles(FinalVerts.Num());

			const int32 NumVerts = FinalVerts.Num();
			for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
			{
				TriMeshParticles.X(VertIndex) = FinalVerts[VertIndex];
			}

			// Build chaos triangle list. #BGTODO Just make the clean function take these types instead of double copying
			const int32 NumTriangles = FinalIndices.Num() / 3;
			bool bHasMaterials = InParams.TriangleMeshDesc.MaterialIndices.Num() > 0;
			TArray<uint16> MaterialIndices;

			auto LambdaHelper = [&](auto& Triangles)
			{
				if(bHasMaterials)
				{
					MaterialIndices.Reserve(NumTriangles);
				}

				Triangles.Reserve(NumTriangles);
				for(int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
				{
					// Only add this triangle if it is valid
					const int32 BaseIndex = TriangleIndex * 3;
					const bool bIsValidTriangle = Chaos::FConvexBuilder::IsValidTriangle(
						FinalVerts[FinalIndices[BaseIndex]],
						FinalVerts[FinalIndices[BaseIndex + 1]],
						FinalVerts[FinalIndices[BaseIndex + 2]]);

					// TODO: Figure out a proper way to handle this. Could these edges get sewn together? Is this important?
					//if (ensureMsgf(bIsValidTriangle, TEXT("FChaosDerivedDataCooker::BuildTriangleMeshes(): Trimesh attempted cooked with invalid triangle!")));
					if(bIsValidTriangle)
					{
						Triangles.Add(Chaos::TVector<int32, 3>(FinalIndices[BaseIndex], FinalIndices[BaseIndex + 1], FinalIndices[BaseIndex + 2]));

						if(bHasMaterials)
						{
							if(EnableMeshClean)
							{
								if(!ensure(OutFaceRemap.IsValidIndex(TriangleIndex)))
								{
									MaterialIndices.Empty();
									bHasMaterials = false;
								}
								else
								{
									const int32 OriginalIndex = OutFaceRemap[TriangleIndex];

									if(ensure(InParams.TriangleMeshDesc.MaterialIndices.IsValidIndex(OriginalIndex)))
									{
										MaterialIndices.Add(InParams.TriangleMeshDesc.MaterialIndices[OriginalIndex]);
									}
									else
									{
										MaterialIndices.Empty();
										bHasMaterials = false;
									}
								}
							}
							else
							{
								if(ensure(InParams.TriangleMeshDesc.MaterialIndices.IsValidIndex(TriangleIndex)))
								{
									MaterialIndices.Add(InParams.TriangleMeshDesc.MaterialIndices[TriangleIndex]);
								}
								else
								{
									MaterialIndices.Empty();
									bHasMaterials = false;
								}
							}

						}
					}
				}

				TUniquePtr<TArray<int32>> OutFaceRemapPtr = MakeUnique<TArray<int32>>(OutFaceRemap);
				TUniquePtr<TArray<int32>> OutVertexRemapPtr = Chaos::TriMeshPerPolySupport ? MakeUnique<TArray<int32>>(OutVertexRemap) : nullptr;
				OutTriangleMeshes.Emplace(new Chaos::FTriangleMeshImplicitObject(MoveTemp(TriMeshParticles), MoveTemp(Triangles), MoveTemp(MaterialIndices), MoveTemp(OutFaceRemapPtr), MoveTemp(OutVertexRemapPtr)));
			};

			if(FinalVerts.Num() < TNumericLimits<uint16>::Max())
			{
				TArray<Chaos::TVector<uint16, 3>> TrianglesSmallIdx;
				LambdaHelper(TrianglesSmallIdx);
			}
			else
			{
				TArray<Chaos::TVector<int32, 3>> TrianglesLargeIdx;
				LambdaHelper(TrianglesLargeIdx);
			}
		}
	}

	FCookHelper::FCookHelper(UBodySetup* InSetup)
		: SourceSetup(InSetup)
	{
		check(SourceSetup);
		EPhysXMeshCookFlags TempFlags = static_cast<EPhysXMeshCookFlags>(0);
		SourceSetup->GetCookInfo(CookInfo, TempFlags);
	}

	void FCookHelper::Cook()
	{
		Cooking::BuildConvexMeshes(SimpleImplicits, CookInfo);
		Cooking::BuildTriangleMeshes(ComplexImplicits, FaceRemap, VertexRemap, CookInfo);

		if(CookInfo.bSupportUVFromHitResults)
		{
			UVInfo.FillFromTriMesh(CookInfo.TriangleMeshDesc);
		}

		if(!CookInfo.bSupportFaceRemap)
		{
			FaceRemap.Empty();
		}
	}

	void FCookHelper::CookAsync(FSimpleDelegateGraphTask::FDelegate CompletionDelegate)
	{
		Cook();
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CompletionDelegate, GET_STATID(STAT_PhysXCooking), nullptr, ENamedThreads::GameThread);
	}

	bool FCookHelper::HasWork() const
	{
		return CookInfo.bCookTriMesh || CookInfo.bCookNonMirroredConvex || CookInfo.bCookMirroredConvex;
	}
}

#endif