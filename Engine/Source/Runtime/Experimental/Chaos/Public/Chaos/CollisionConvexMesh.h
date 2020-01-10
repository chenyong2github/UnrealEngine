// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Box.h"
#include "TriangleMesh.h"
#include "Particles.h"

namespace Chaos
{
	class FConvexBuilder
	{
	public:
		static bool IsValidTriangle(const FVec3& A, const FVec3& B, const FVec3& C)
		{
			const FVec3 BA = B - A;
			const FVec3 CA = C - A;
			const FVec3 Cross = FVec3::CrossProduct(BA, CA);
			return Cross.Size() > 1e-4;
		}

		static void Build(const TParticles<FReal, 3>& InParticles, TArray <TPlane<FReal, 3>>& OutPlanes, TArray<TArray<int32>>& OutFaceIndices, TParticles<FReal, 3>& OutSurfaceParticles, TAABB<FReal, 3>& OutLocalBounds)
		{
			OutPlanes.Reset();
			OutSurfaceParticles.Resize(0);
			OutLocalBounds = TAABB<FReal, 3>::EmptyAABB();

			const uint32 NumParticles = InParticles.Size();
			if(NumParticles == 0)
			{
				return;
			}

			OutLocalBounds = TAABB<FReal, 3>(InParticles.X(0), InParticles.X(0));
			for(uint32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
			{
				OutLocalBounds.GrowToInclude(InParticles.X(ParticleIndex));
			}

			if(NumParticles >= 4)
			{
				TArray<TVector<int32, 3>> Indices;
				BuildConvexHull(InParticles, Indices);
				OutPlanes.Reserve(Indices.Num());
				TMap<int32, int32> IndexMap; // maps original particle indices to output particle indices
				int32 NewIdx = 0;

				const auto AddIndex = [&IndexMap, &NewIdx](const int32 OriginalIdx)
				{
					if (int32* Idx = IndexMap.Find(OriginalIdx))
					{
						return *Idx;
					}
					IndexMap.Add(OriginalIdx, NewIdx);
					return NewIdx++;
				};

				for(const TVector<int32, 3>& Idx : Indices)
				{
					FVec3 Vs[3] = {InParticles.X(Idx[0]), InParticles.X(Idx[1]), InParticles.X(Idx[2])};
					const FVec3 Normal = FVec3::CrossProduct(Vs[1] - Vs[0], Vs[2] - Vs[0]).GetUnsafeNormal();
					OutPlanes.Add(TPlane<FReal, 3>(Vs[0], Normal));
					TArray<int32> FaceIndices;
					FaceIndices.SetNum(3);
					FaceIndices[0] = AddIndex(Idx[0]);
					FaceIndices[1] = AddIndex(Idx[1]);
					FaceIndices[2] = AddIndex(Idx[2]);
					OutFaceIndices.Add(FaceIndices);
				}

				OutSurfaceParticles.AddParticles(IndexMap.Num());
				for(const auto& Elem : IndexMap)
				{
					OutSurfaceParticles.X(Elem.Value) = InParticles.X(Elem.Key);
				}
			}
			else if(NumParticles == 3)
			{
				//special support for triangle
				const bool bIsValidTriangle = IsValidTriangle(InParticles.X(0), InParticles.X(1), InParticles.X(2));

				//TODO_SQ_IMPLEMENTATION: should do proper cleanup to avoid this
				if (ensureMsgf(bIsValidTriangle, TEXT("FConvexBuilder::Build(): Generated invalid triangle!")))
				{
					FVec3 Normal = FVec3::CrossProduct(InParticles.X(1) - InParticles.X(0), InParticles.X(2) - InParticles.X(0)).GetSafeNormal();
					OutPlanes.Add(TPlane<FReal, 3>(InParticles.X(0), Normal));
					OutSurfaceParticles.AddParticles(3);
					OutSurfaceParticles.X(0) = InParticles.X(0);
					OutSurfaceParticles.X(1) = InParticles.X(1);
					OutSurfaceParticles.X(2) = InParticles.X(2);

					TArray<int32> FaceIndices;
					FaceIndices.SetNum(3);
					FaceIndices[0] = 0; 
					FaceIndices[1] = 1;
					FaceIndices[2] = 2;
					OutFaceIndices.Add(FaceIndices);
				}
			}
		}

		static void BuildConvexHull(const TParticles<FReal, 3>& InParticles, TArray<TVector<int32, 3>>& OutIndices)
		{
			OutIndices.Reset();
			FConvexFace* Faces = BuildInitialHull(InParticles);
			if(Faces == nullptr)
			{
				return;
			}
			FConvexFace DummyFace(Faces->Plane);
			DummyFace.Prev = nullptr;
			DummyFace.Next = Faces;
			Faces->Prev = &DummyFace;

			FHalfEdge* ConflictV = FindConflictVertex(InParticles, DummyFace.Next);
			while(ConflictV)
			{
				AddVertex(InParticles, ConflictV);
				ConflictV = FindConflictVertex(InParticles, DummyFace.Next);
			}

			FConvexFace* Cur = DummyFace.Next;
			while(Cur)
			{
				//todo(ocohen): this assumes faces are triangles, not true once face merging is added
				OutIndices.Add(TVector<int32, 3>(Cur->FirstEdge->Vertex, Cur->FirstEdge->Next->Vertex, Cur->FirstEdge->Next->Next->Vertex));
				delete Cur->FirstEdge->Next;
				delete Cur->FirstEdge->Prev;
				delete Cur->FirstEdge;
				FConvexFace* Next = Cur->Next;
				delete Cur;
				Cur = Next;
			}
		}

		static TTriangleMesh<FReal> BuildConvexHullTriMesh(const TParticles<FReal, 3>& InParticles)
		{
			TArray<TVector<int32, 3>> Indices;
			BuildConvexHull(InParticles, Indices);
			return TTriangleMesh<FReal>(MoveTemp(Indices));
		}

		static CHAOS_API bool IsPerformanceWarning(int32 NumPlanes, int32 NumParticles)
		{
			if (!PerformGeometryCheck)
			{
				return false;
			}

			return (NumParticles > ParticlesThreshold);
		}

		static CHAOS_API bool IsGeometryReductionEnabled()
		{
			return (PerformGeometryReduction>0)?true:false;
		}

		static FString PerformanceWarningString(int32 NumPlanes, int32 NumParticles)
		{
			return FString::Printf(TEXT("Planes %d, SurfaceParticles %d"), NumPlanes, NumParticles);
		}

		static CHAOS_API void Simplify(TArray <TPlane<FReal, 3>>& InOutPlanes, TArray<TArray<int32>>& InOutFaces, TParticles<FReal, 3>& InOutParticles, TAABB<FReal, 3>& InOutLocalBounds)
		{
			struct TPair
			{
				TPair() : A(-1), B(-1) {}
				uint32 A;
				uint32 B;
			};

			uint32 NumberOfParticlesRequired = ParticlesThreshold;
			uint32 NumberOfParticlesWeHave = InOutParticles.Size();
			int32 NumToDelete = NumberOfParticlesWeHave - NumberOfParticlesRequired;

			uint32 Size = InOutParticles.Size();
			TArray<FVec3> Particles;
			Particles.AddUninitialized(Size);
			for (uint32 A = 0; A < Size; A++)
			{
				Particles[A] = InOutParticles.X(A);
			}

			TArray<bool> IsDeleted;
			IsDeleted.Reset();
			IsDeleted.Init(false, Size);

			if (NumToDelete > 0)
			{
				for (uint32 Iteration = 0; Iteration < (uint32)NumToDelete; Iteration++)
				{
					TPair ClosestPair;
					float ClosestDistSqr = FLT_MAX;

					for (uint32 A = 0; A < (Size - 1); A++)
					{
						if (!IsDeleted[A])
						{
							for (uint32 B = A + 1; B < Size; B++)
							{
								if (!IsDeleted[B])
								{
									FVec3 Vec = Particles[A] - Particles[B];
									float LengthSqr = Vec.SizeSquared();
									if (LengthSqr < ClosestDistSqr)
									{
										ClosestDistSqr = LengthSqr;
										ClosestPair.A = A;
										ClosestPair.B = B;
									}
								}
							}
						}
					}

					if (ClosestPair.A != -1)
					{
						// merge to mid point
						Particles[ClosestPair.A] = Particles[ClosestPair.A] + (Particles[ClosestPair.B] - Particles[ClosestPair.A]) * 0.5f;
						IsDeleted[ClosestPair.B] = true;
					}
				}
			}

			TParticles<FReal, 3> TmpParticles;
			for (int Idx = 0; Idx < Particles.Num(); Idx++)
			{
				// Only add particles that have not been merged away
				if (!IsDeleted[Idx])
				{
					TmpParticles.AddParticles(1);
					TmpParticles.X(TmpParticles.Size() - 1) = Particles[Idx];
				}
			}

			Build(TmpParticles, InOutPlanes, InOutFaces, InOutParticles, InOutLocalBounds);
			check(InOutParticles.Size() > 3);
		}

		// CVars variables for controlling geometry complexity checking and simplification
#if PLATFORM_MAC || PLATFORM_LINUX
		static CHAOS_API int32 PerformGeometryCheck;
		static CHAOS_API int32 PerformGeometryReduction;
		static CHAOS_API int32 ParticlesThreshold;
#else
		static int32 PerformGeometryCheck;
		static int32 PerformGeometryReduction;
		static int32 ParticlesThreshold;
#endif

	private:

		struct FHalfEdge;
		struct FConvexFace
		{
			FConvexFace(const TPlane<FReal, 3>& FacePlane)
				: ConflictList(nullptr)
				, Plane(FacePlane)
			{
			}

			FHalfEdge* FirstEdge;
			FHalfEdge* ConflictList; //Note that these half edges are really just free verts grouped together
			TPlane<FReal, 3> Plane;
			FConvexFace* Prev;
			FConvexFace* Next; //these have no geometric meaning, just used for book keeping
		};

		struct FHalfEdge
		{
			FHalfEdge(int32 InVertex=-1)
				: Vertex(InVertex) 
			{}
			int32 Vertex;
			FHalfEdge* Prev;
			FHalfEdge* Next;
			FHalfEdge* Twin;
			FConvexFace* Face;
		};

		static FVec3 ComputeFaceNormal(const FVec3& A, const FVec3& B, const FVec3& C)
		{
			return FVec3::CrossProduct((B - A), (C - A));
		}

		static FConvexFace* CreateFace(const TParticles<FReal, 3>& InParticles, FHalfEdge* RS, FHalfEdge* ST, FHalfEdge* TR)
		{
			RS->Prev = TR;
			RS->Next = ST;
			ST->Prev = RS;
			ST->Next = TR;
			TR->Prev = ST;
			TR->Next = RS;
			FVec3 RSTNormal = ComputeFaceNormal(InParticles.X(RS->Vertex), InParticles.X(ST->Vertex), InParticles.X(TR->Vertex));
			const FReal RSTNormalSize = RSTNormal.Size();
			check(RSTNormalSize > 1e-4);
			RSTNormal = RSTNormal * (1 / RSTNormalSize);
			FConvexFace* RST = new FConvexFace(TPlane<FReal, 3>(InParticles.X(RS->Vertex), RSTNormal));
			RST->FirstEdge = RS;
			RS->Face = RST;
			ST->Face = RST;
			TR->Face = RST;
			return RST;
		}

		static void StealConflictList(const TParticles<FReal, 3>& InParticles, FHalfEdge* OldList, FConvexFace** Faces, int32 NumFaces)
		{
			FHalfEdge* Cur = OldList;
			while(Cur)
			{
				FReal MaxD = 1e-4;
				int32 MaxIdx = -1;
				for(int32 Idx = 0; Idx < NumFaces; ++Idx)
				{
					FReal Distance = Faces[Idx]->Plane.SignedDistance(InParticles.X(Cur->Vertex));
					if(Distance > MaxD)
					{
						MaxD = Distance;
						MaxIdx = Idx;
					}
				}

				bool bDeleteVertex = MaxIdx == -1;
				if(!bDeleteVertex)
				{
					//let's make sure faces created with this new conflict vertex will be valid. The plane check above is not sufficient because long thin triangles will have a plane with its point at one of these. Combined with normal and precision we can have errors
					auto PretendNormal = [&InParticles](FHalfEdge* A, FHalfEdge* B, FHalfEdge* C) {
						return FVec3::CrossProduct(InParticles.X(B->Vertex) - InParticles.X(A->Vertex), InParticles.X(C->Vertex) - InParticles.X(A->Vertex)).SizeSquared();
					};
					FHalfEdge* Edge = Faces[MaxIdx]->FirstEdge;
					do
					{
						if(PretendNormal(Edge->Prev, Edge, Cur) < 1e-4)
						{
							bDeleteVertex = true;
							break;
						}
						Edge = Edge->Next;
					} while(Edge != Faces[MaxIdx]->FirstEdge);
				}

				if(!bDeleteVertex)
				{
					FHalfEdge* Next = Cur->Next;
					FHalfEdge*& ConflictList = Faces[MaxIdx]->ConflictList;
					if(ConflictList)
					{
						ConflictList->Prev = Cur;
					}
					Cur->Next = ConflictList;
					ConflictList = Cur;
					Cur->Prev = nullptr;
					Cur = Next;
				}
				else
				{
					//point is contained, we can delete it
					FHalfEdge* Next = Cur->Next;
					delete Cur; //todo(ocohen): better allocation strategy
					Cur = Next;
				}
			}
		}

		static FConvexFace* BuildInitialHull(const TParticles<FReal, 3>& InParticles)
		{
			if(InParticles.Size() < 4) //not enough points
			{
				return nullptr;
			}

			constexpr FReal Epsilon = 1e-4;

			const int32 NumParticles = InParticles.Size();

			//We store the vertex directly in the half-edge. We use its next to group free vertices by context list
			//create a starting triangle by finding min/max on X and max on Y
			FReal MinX = TNumericLimits<FReal>::Max();
			FReal MaxX = TNumericLimits<FReal>::Lowest();
			FHalfEdge* A = nullptr; //min x
			FHalfEdge* B = nullptr; //max x
			FHalfEdge DummyHalfEdge(-1);
			DummyHalfEdge.Prev = nullptr;
			DummyHalfEdge.Next = nullptr;
			FHalfEdge* Prev = &DummyHalfEdge;

			for(int32 i = 0; i < NumParticles; ++i)
			{
				FHalfEdge* VHalf = new FHalfEdge(i); //todo(ocohen): preallocate these
				Prev->Next = VHalf;
				VHalf->Prev = Prev;
				VHalf->Next = nullptr;
				const FVec3& V = InParticles.X(i);

				if(V[0] < MinX)
				{
					MinX = V[0];
					A = VHalf;
				}
				if(V[0] > MaxX)
				{
					MaxX = V[0];
					B = VHalf;
				}

				Prev = VHalf;
			}

			check(A && B);
			if(A == B || (InParticles.X(A->Vertex) - InParticles.X(B->Vertex)).SizeSquared() < Epsilon) //infinitely thin
			{
				return nullptr;
			}

			//remove A and B from conflict list
			A->Prev->Next = A->Next;
			if(A->Next)
			{
				A->Next->Prev = A->Prev;
			}
			B->Prev->Next = B->Next;
			if(B->Next)
			{
				B->Next->Prev = B->Prev;
			}

			//find C so that we get the biggest base triangle
			FReal MaxTriSize = Epsilon;
			const FVec3 AToB = InParticles.X(B->Vertex) - InParticles.X(A->Vertex);
			FHalfEdge* C = nullptr;
			for(FHalfEdge* V = DummyHalfEdge.Next; V; V = V->Next)
			{
				FReal TriSize = FVec3::CrossProduct(AToB, InParticles.X(V->Vertex) - InParticles.X(A->Vertex)).SizeSquared();
				if(TriSize > MaxTriSize)
				{
					MaxTriSize = TriSize;
					C = V;
				}
			}

			if(C == nullptr) //biggest triangle is tiny
			{
				return nullptr;
			}

			//remove C from conflict list
			C->Prev->Next = C->Next;
			if(C->Next)
			{
				C->Next->Prev = C->Prev;
			}

			//find farthest D along normal
			const FVec3 AToC = InParticles.X(C->Vertex) - InParticles.X(A->Vertex);
			const FVec3 Normal = FVec3::CrossProduct(AToB, AToC);

			FReal MaxPosDistance = Epsilon;
			FReal MaxNegDistance = Epsilon;
			FHalfEdge* PosD = nullptr;
			FHalfEdge* NegD = nullptr;
			for(FHalfEdge* V = DummyHalfEdge.Next; V; V = V->Next)
			{
				FReal Dot = FVec3::DotProduct(InParticles.X(V->Vertex) - InParticles.X(A->Vertex), Normal);
				if(Dot > MaxPosDistance)
				{
					MaxPosDistance = Dot;
					PosD = V;
				}
				if(-Dot > MaxNegDistance)
				{
					MaxNegDistance = -Dot;
					NegD = V;
				}
			}

			if(MaxNegDistance == Epsilon && MaxPosDistance == Epsilon)
			{
				return nullptr; //plane
			}

			const bool bPositive = MaxNegDistance < MaxPosDistance;
			FHalfEdge* D = bPositive ? PosD : NegD;

			//remove D from conflict list
			D->Prev->Next = D->Next;
			if(D->Next)
			{
				D->Next->Prev = D->Prev;
			}

			//we must now create the 3 faces. Face must be oriented CCW around normal and positive normal should face out
			//Note we are now using A,B,C,D as edges. We can only use one edge per face so once they're used we'll need new ones
			FHalfEdge* Edges[4] = {A, B, C, D};

			//The base is a plane with Edges[0], Edges[1], Edges[2]. The order depends on which side D is on
			if(bPositive)
			{
				//D is on the positive side of Edges[0], Edges[1], Edges[2] so we must reorder it
				FHalfEdge* Tmp = Edges[0];
				Edges[0] = Edges[1];
				Edges[1] = Tmp;
			}

			FConvexFace* Faces[4];
			Faces[0] = CreateFace(InParticles, Edges[0], Edges[1], Edges[2]); //base
			Faces[1] = CreateFace(InParticles, new FHalfEdge(Edges[1]->Vertex), new FHalfEdge(Edges[0]->Vertex), Edges[3]);
			Faces[2] = CreateFace(InParticles, new FHalfEdge(Edges[0]->Vertex), new FHalfEdge(Edges[2]->Vertex), new FHalfEdge(Edges[3]->Vertex));
			Faces[3] = CreateFace(InParticles, new FHalfEdge(Edges[2]->Vertex), new FHalfEdge(Edges[1]->Vertex), new FHalfEdge(Edges[3]->Vertex));

			auto MakeTwins = [](FHalfEdge* E1, FHalfEdge* E2) {
				E1->Twin = E2;
				E2->Twin = E1;
			};
			//mark twins so half edge can cross faces
			MakeTwins(Edges[0], Faces[1]->FirstEdge); //0-1 1-0
			MakeTwins(Edges[1], Faces[3]->FirstEdge); //1-2 2-1
			MakeTwins(Edges[2], Faces[2]->FirstEdge); //2-0 0-2
			MakeTwins(Faces[1]->FirstEdge->Next, Faces[2]->FirstEdge->Prev); //0-3 3-0
			MakeTwins(Faces[1]->FirstEdge->Prev, Faces[3]->FirstEdge->Next); //3-1 1-3
			MakeTwins(Faces[2]->FirstEdge->Next, Faces[3]->FirstEdge->Prev); //2-3 3-2

			Faces[0]->Prev = nullptr;
			for(int i = 1; i < 4; ++i)
			{
				Faces[i - 1]->Next = Faces[i];
				Faces[i]->Prev = Faces[i - 1];
			}
			Faces[3]->Next = nullptr;

			//split up the conflict list
			StealConflictList(InParticles, DummyHalfEdge.Next, Faces, 4);
			return Faces[0];
		}

		static FHalfEdge* FindConflictVertex(const TParticles<FReal, 3>& InParticles, FConvexFace* FaceList)
		{
			for(FConvexFace* CurFace = FaceList; CurFace; CurFace = CurFace->Next)
			{
				FReal MaxD = TNumericLimits<FReal>::Lowest();
				FHalfEdge* MaxV = nullptr;
				for(FHalfEdge* CurFaceVertex = CurFace->ConflictList; CurFaceVertex; CurFaceVertex = CurFaceVertex->Next)
				{
					//is it faster to cache this from stealing stage?
					FReal Dist = FVec3::DotProduct(InParticles.X(CurFaceVertex->Vertex), CurFace->Plane.Normal());
					if(Dist > MaxD)
					{
						MaxD = Dist;
						MaxV = CurFaceVertex;
					}
				}
				check(CurFace->ConflictList == nullptr || MaxV);
				if(MaxV)
				{
					if(MaxV->Prev)
					{
						MaxV->Prev->Next = MaxV->Next;
					}
					if(MaxV->Next)
					{
						MaxV->Next->Prev = MaxV->Prev;
					}
					if(MaxV == CurFace->ConflictList)
					{
						CurFace->ConflictList = MaxV->Next;
					}
					MaxV->Face = CurFace;
					return MaxV;
				}
			}

			return nullptr;
		}

		static void BuildHorizon(const TParticles<FReal, 3>& InParticles, FHalfEdge* ConflictV, TArray<FHalfEdge*>& HorizonEdges, TArray<FConvexFace*>& FacesToDelete)
		{
			//We must flood fill from the initial face and mark edges of faces the conflict vertex cannot see
			//In order to return a CCW ordering we must traverse each face in CCW order from the edge we crossed over
			//This should already be the ordering in the half edge
			const FReal Epsilon = 1e-1;
			const FVec3 V = InParticles.X(ConflictV->Vertex);
			TSet<FConvexFace*> Processed;
			TArray<FHalfEdge*> Queue;
			check(ConflictV->Face);
			Queue.Add(ConflictV->Face->FirstEdge->Prev); //stack pops so reverse order
			Queue.Add(ConflictV->Face->FirstEdge->Next);
			Queue.Add(ConflictV->Face->FirstEdge);
			FacesToDelete.Add(ConflictV->Face);
			while(Queue.Num())
			{
				FHalfEdge* Edge = Queue.Pop(/*bAllowShrinking=*/false);
				Processed.Add(Edge->Face);
				FHalfEdge* Twin = Edge->Twin;
				FConvexFace* NextFace = Twin->Face;
				if(Processed.Contains(NextFace))
				{
					continue;
				}
				const FReal Distance = NextFace->Plane.SignedDistance(V);
				if(Distance > Epsilon)
				{
					Queue.Add(Twin->Prev); //stack pops so reverse order
					Queue.Add(Twin->Next);
					FacesToDelete.Add(NextFace);
				}
				else
				{
					HorizonEdges.Add(Edge);
				}
			}
		}

		static void BuildFaces(const TParticles<FReal, 3>& InParticles, const FHalfEdge* ConflictV, const TArray<FHalfEdge*>& HorizonEdges, const TArray<FConvexFace*> OldFaces, TArray<FConvexFace*>& NewFaces)
		{
			//The HorizonEdges are in CCW order. We must make new faces and edges to join from ConflictV to these edges
			check(HorizonEdges.Num() >= 3);
			NewFaces.Reserve(HorizonEdges.Num());
			FHalfEdge* PrevEdge = nullptr;
			for(int32 HorizonIdx = 0; HorizonIdx < HorizonEdges.Num(); ++HorizonIdx)
			{
				FHalfEdge* OriginalEdge = HorizonEdges[HorizonIdx];
				FHalfEdge* NewHorizonEdge = new FHalfEdge(OriginalEdge->Vertex);
				NewHorizonEdge->Twin = OriginalEdge->Twin; //swap edges
				NewHorizonEdge->Twin->Twin = NewHorizonEdge;
				FHalfEdge* HorizonNext = new FHalfEdge(OriginalEdge->Next->Vertex);
				check(HorizonNext->Vertex == HorizonEdges[(HorizonIdx + 1) % HorizonEdges.Num()]->Vertex); //should be ordered properly
				FHalfEdge* V = new FHalfEdge(ConflictV->Vertex);
				V->Twin = PrevEdge;
				if(PrevEdge)
				{
					PrevEdge->Twin = V;
				}
				PrevEdge = HorizonNext;

				//link new faces together
				FConvexFace* NewFace = CreateFace(InParticles, NewHorizonEdge, HorizonNext, V);
				if(NewFaces.Num() > 0)
				{
					NewFace->Prev = NewFaces[NewFaces.Num() - 1];
					NewFaces[NewFaces.Num() - 1]->Next = NewFace;
				}
				else
				{
					NewFace->Prev = nullptr;
				}
				NewFaces.Add(NewFace);
			}

			check(PrevEdge);
			NewFaces[0]->FirstEdge->Prev->Twin = PrevEdge;
			PrevEdge->Twin = NewFaces[0]->FirstEdge->Prev;
			NewFaces[NewFaces.Num() - 1]->Next = nullptr;

			//redistribute conflict list
			for(FConvexFace* OldFace : OldFaces)
			{
				StealConflictList(InParticles, OldFace->ConflictList, &NewFaces[0], NewFaces.Num());
			}

			//insert all new faces after conflict vertex face
			FConvexFace* OldFace = ConflictV->Face;
			FConvexFace* StartFace = NewFaces[0];
			FConvexFace* EndFace = NewFaces[NewFaces.Num() - 1];
			if(OldFace->Next)
			{
				OldFace->Next->Prev = EndFace;
			}
			EndFace->Next = OldFace->Next;
			OldFace->Next = StartFace;
			StartFace->Prev = OldFace;
		}

		static void AddVertex(const TParticles<FReal, 3>& InParticles, FHalfEdge* ConflictV)
		{
			TArray<FHalfEdge*> HorizonEdges;
			TArray<FConvexFace*> FacesToDelete;
			BuildHorizon(InParticles, ConflictV, HorizonEdges, FacesToDelete);

			TArray<FConvexFace*> NewFaces;
			BuildFaces(InParticles, ConflictV, HorizonEdges, FacesToDelete, NewFaces);

			for(FConvexFace* Face : FacesToDelete)
			{
				FHalfEdge* Edge = Face->FirstEdge;
				do
				{
					FHalfEdge* Next = Edge->Next;
					delete Edge;
					Edge = Next;
				} while(Edge != Face->FirstEdge);
				if(Face->Prev)
				{
					Face->Prev->Next = Face->Next;
				}
				if(Face->Next)
				{
					Face->Next->Prev = Face->Prev;
				}
				delete Face;
			}

			//todo(ocohen): need to explicitly test for merge failures. Coplaner, nonconvex, etc...
			//getting this in as is for now to unblock other systems
		}

	};
}
