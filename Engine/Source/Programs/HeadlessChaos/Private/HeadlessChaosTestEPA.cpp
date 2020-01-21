// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestEPA.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/EPA.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/GJK.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace ChaosTest
{
	using namespace Chaos;

	template <typename T>
	void ValidFace(const TVec3<T>* Verts, const TArray<TEPAEntry<T>>& TetFaces, int32 Idx)
	{
		const TEPAEntry<T>& Entry = TetFaces[Idx];

		//does not contain vertex associated with face
		EXPECT_NE(Entry.IdxBuffer[0], Idx);
		EXPECT_NE(Entry.IdxBuffer[1], Idx);
		EXPECT_NE(Entry.IdxBuffer[2], Idx);

		//doesn't have itself as adjacent face
		EXPECT_NE(Entry.AdjFaces[0], Idx);
		EXPECT_NE(Entry.AdjFaces[1], Idx);
		EXPECT_NE(Entry.AdjFaces[2], Idx);

		//adjacent edges and faces are valid for both sides of the edge
		EXPECT_EQ(TetFaces[Entry.AdjFaces[0]].AdjFaces[Entry.AdjEdges[0]], Idx);
		EXPECT_EQ(TetFaces[Entry.AdjFaces[1]].AdjFaces[Entry.AdjEdges[1]], Idx);
		EXPECT_EQ(TetFaces[Entry.AdjFaces[2]].AdjFaces[Entry.AdjEdges[2]], Idx);

		//make sure that adjacent faces share vertices
		//src dest on the edge of face 0 matches to dest src of face 1
		for (int EdgeIdx = 0; EdgeIdx < 3; ++EdgeIdx)
		{
			const int32 FromFace0 = Entry.IdxBuffer[EdgeIdx];
			const int32 ToFace0 = Entry.IdxBuffer[(EdgeIdx+1)%3];
			const int32 Face1EdgeIdx = Entry.AdjEdges[EdgeIdx];
			const TEPAEntry<T>& Face1 = TetFaces[Entry.AdjFaces[EdgeIdx]];
			const int32 FromFace1 = Face1.IdxBuffer[Face1EdgeIdx];
			const int32 ToFace1 = Face1.IdxBuffer[(Face1EdgeIdx+1)%3];
			EXPECT_EQ(FromFace0, ToFace1);
			EXPECT_EQ(FromFace1, ToFace0);
			
		}
		switch (Entry.AdjEdges[0])
		{
		case 0:
		{
			EXPECT_EQ(Entry.IdxBuffer[0], TetFaces[Entry.AdjFaces[0]].IdxBuffer[1]);
			EXPECT_EQ(Entry.IdxBuffer[1], TetFaces[Entry.AdjFaces[0]].IdxBuffer[0]);
			break;
		}
		case 1:
		{
			EXPECT_EQ(Entry.IdxBuffer[0], TetFaces[Entry.AdjFaces[0]].IdxBuffer[2]);
			EXPECT_EQ(Entry.IdxBuffer[1], TetFaces[Entry.AdjFaces[0]].IdxBuffer[1]);
			break;
		}
		case 2:
		{
			EXPECT_EQ(Entry.IdxBuffer[0], TetFaces[Entry.AdjFaces[0]].IdxBuffer[0]);
			EXPECT_EQ(Entry.IdxBuffer[1], TetFaces[Entry.AdjFaces[0]].IdxBuffer[2]);
			break;
		}
		default: break;
		}

		EXPECT_LT(TVec3<T>::DotProduct(Verts[Idx], Entry.PlaneNormal), 0);	//normal faces out

		EXPECT_GE(Entry.Distance, 0);	//positive distance since origin is inside tet

		EXPECT_NEAR(Entry.DistanceToPlane(Verts[Entry.IdxBuffer[0]]), 0, 1e-6);
		EXPECT_NEAR(Entry.DistanceToPlane(Verts[Entry.IdxBuffer[1]]), 0, 1e-6);
		EXPECT_NEAR(Entry.DistanceToPlane(Verts[Entry.IdxBuffer[2]]), 0, 1e-6);
	}

	template <typename T>
	TVec3<T> ErrorSupport(const TVec3<T>& V)
	{
		check(false);
		return TVec3<T>(0);
	}

	template <typename T>
	void EPAInitTest()
	{

		//make sure faces are properly oriented
		{
			TArray<TVec3<T>> VertsA = { {-1,-1,1}, {-1,-1,-1}, {-1,1,-1}, {1,1,-1} };
			TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };
			TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ErrorSupport<T>, ErrorSupport<T>);

			EXPECT_EQ(TetFaces.Num(), 4);
			for (int i = 0; i < TetFaces.Num(); ++i)
			{
				ValidFace(VertsA.GetData(), TetFaces, i);
			}
		}

		{
			TArray<TVec3<T>> VertsA = { {-1,-1,-1}, {-1,-1,1}, {-1,1,-1}, {1,1,-1} };
			TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };
			TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ErrorSupport<T>, ErrorSupport<T>);

			EXPECT_EQ(TetFaces.Num(), 4);
			for (int i = 0; i < TetFaces.Num(); ++i)
			{
				ValidFace(VertsA.GetData(), TetFaces, i);
			}
		}

		auto EmptySupport = [](const TVec3<T>& V) { return TVec3<T>(0); };

		//triangle
		{
			TVec3<T> AllVerts[] = { {0,-1,1 + 1 / (T)3}, {0,-1,-1 + 1 / (T)3}, {0,1,-1 + 1 / (T)3},{-1,0,0}, {0.5,0,0} };

			auto ASupport = [&](const TVec3<T>& V)
			{
				TVec3<T> Best = AllVerts[0];
				for (const TVec3<T>& Vert : AllVerts)
				{
					if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
					{
						Best = Vert;
					}
				}
				return Best;
			};

			auto ASupportNoPositiveX = [&](const TVec3<T>& V)
			{
				TVec3<T> Best = AllVerts[0];
				for (const TVec3<T>& Vert : AllVerts)
				{
					if (Vert.X > 0)
					{
						continue;
					}
					if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
					{
						Best = Vert;
					}
				}
				return Best;
			};

			auto ASupportNoX = [&](const TVec3<T>& V)
			{
				TVec3<T> Best = AllVerts[0];
				for (const TVec3<T>& Vert : AllVerts)
				{
					if (Vert.X > 0 || Vert.X < 0)
					{
						continue;
					}
					if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
					{
						Best = Vert;
					}
				}
				return Best;
			};

			//first winding
			{
				TArray<TVec3<T>> VertsA = { AllVerts[0], AllVerts[1], AllVerts[2] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupport, EmptySupport);
				EXPECT_VECTOR_NEAR(VertsA[3], AllVerts[3], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[3], TVec3<T>(0), 1e-4);

				EXPECT_EQ(TetFaces.Num(), 4);
				for (int i = 0; i < TetFaces.Num(); ++i)
				{
					ValidFace(VertsA.GetData(), TetFaces, i);
				}

				T Penetration;
				TVec3<T> Dir, WitnessA, WitnessB;

				//Try EPA. Note that we are IGNORING the positive x vert to ensure a triangle right on the origin boundary works
				EPA(VertsA, VertsB, ASupportNoPositiveX, EmptySupport, Penetration, Dir, WitnessA, WitnessB);
				EXPECT_NEAR(Penetration, 0, 1e-4);
				EXPECT_VECTOR_NEAR(Dir, TVec3<T>(1,0,0), 1e-4);
				EXPECT_VECTOR_NEAR(WitnessA, TVec3<T>(0), 1e-4);
				EXPECT_VECTOR_NEAR(WitnessB, TVec3<T>(0), 1e-4);
			}

			//other winding
			{
				TArray<TVec3<T>> VertsA = { AllVerts[1], AllVerts[0], AllVerts[2] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupport, EmptySupport);
				EXPECT_VECTOR_NEAR(VertsA[3], AllVerts[3], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[3], TVec3<T>(0), 1e-4);

				EXPECT_EQ(TetFaces.Num(), 4);
				for (int i = 0; i < TetFaces.Num(); ++i)
				{
					ValidFace(VertsA.GetData(), TetFaces, i);
				}

				T Penetration;
				TVec3<T> Dir, WitnessA, WitnessB;

				//Try EPA. Note that we are IGNORING the positive x vert to ensure a triangle right on the origin boundary works
				EPA(VertsA, VertsB, ASupportNoPositiveX, EmptySupport, Penetration, Dir, WitnessA, WitnessB);
				EXPECT_NEAR(Penetration, 0, 1e-4);
				EXPECT_VECTOR_NEAR(Dir, TVec3<T>(1, 0, 0), 1e-4);
				EXPECT_VECTOR_NEAR(WitnessA, TVec3<T>(0), 1e-4);
				EXPECT_VECTOR_NEAR(WitnessB, TVec3<T>(0), 1e-4);
			}

			//touching triangle
			{
				TArray<TVec3<T>> VertsA = { AllVerts[1], AllVerts[0], AllVerts[2] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupportNoX, EmptySupport);
				EXPECT_EQ(TetFaces.Num(), 0);


				//make sure EPA handles this bad case properly
				VertsA = { AllVerts[1], AllVerts[0], AllVerts[2] };
				VertsB = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

				//touching so penetration 0, normal is 0,0,1
				T Penetration;
				TVec3<T> Dir, WitnessA, WitnessB;
				EXPECT_EQ(EPA(VertsA, VertsB, ASupportNoX, EmptySupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::BadInitialSimplex);
				EXPECT_EQ(Penetration, 0);
				EXPECT_VECTOR_NEAR(Dir, TVec3<T>(0, 0, 1), 1e-7);
				EXPECT_VECTOR_NEAR(WitnessA, TVec3<T>(0), 1e-7);
				EXPECT_VECTOR_NEAR(WitnessB, TVec3<T>(0), 1e-7);
			}
		}

		//line
		{
			TVec3<T> AllVerts[] = { {0,-1,1 + 1 / (T)3}, {0,-1,-1 + 1 / (T)3}, {0,1,-1 + 1 / (T)3},{-1,0,0}, {0.5,0,0} };

			auto ASupport = [&](const TVec3<T>& V)
			{
				TVec3<T> Best = AllVerts[0];
				for (const TVec3<T>& Vert : AllVerts)
				{
					if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
					{
						Best = Vert;
					}
				}
				return Best;
			};

			//first winding
			{
				TArray<TVec3<T>> VertsA = { AllVerts[0], AllVerts[2] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupport, EmptySupport);
				EXPECT_VECTOR_NEAR(VertsA[2], AllVerts[1], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[2], TVec3<T>(0), 1e-4);

				EXPECT_VECTOR_NEAR(VertsA[3], AllVerts[3], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[3], TVec3<T>(0), 1e-4);

				EXPECT_EQ(TetFaces.Num(), 4);
				for (int i = 0; i < TetFaces.Num(); ++i)
				{
					ValidFace(VertsA.GetData(), TetFaces, i);
				}
			}

			//other winding
			{
				TArray<TVec3<T>> VertsA = { AllVerts[2], AllVerts[0] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupport, EmptySupport);
				EXPECT_VECTOR_NEAR(VertsA[2], AllVerts[1], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[2], TVec3<T>(0), 1e-4);

				EXPECT_VECTOR_NEAR(VertsA[3], AllVerts[3], 1e-4);
				EXPECT_VECTOR_NEAR(VertsB[3], TVec3<T>(0), 1e-4);

				EXPECT_EQ(TetFaces.Num(), 4);
				for (int i = 0; i < TetFaces.Num(); ++i)
				{
					ValidFace(VertsA.GetData(), TetFaces, i);
				}
			}

			//touching triangle
			{
				auto ASupportNoX = [&](const TVec3<T>& V)
				{
					TVec3<T> Best = AllVerts[0];
					for (const TVec3<T>& Vert : AllVerts)
					{
						if (Vert.X > 0 || Vert.X < 0)
						{
							continue;
						}
						if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
						{
							Best = Vert;
						}
					}
					return Best;
				};

				TArray<TVec3<T>> VertsA = { AllVerts[2], AllVerts[0] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupportNoX, EmptySupport);
				EXPECT_EQ(TetFaces.Num(), 0);
			}

			//touching line
			{

				auto ASupportNoXOrZ = [&](const TVec3<T>& V)
				{
					TVec3<T> Best = AllVerts[0];
					for (const TVec3<T>& Vert : AllVerts)
					{
						if (Vert.X > 0 || Vert.X < 0 || Vert.Z > 0)
						{
							continue;
						}
						if (TVec3<T>::DotProduct(Vert, V) > TVec3<T>::DotProduct(Best, V))
						{
							Best = Vert;
						}
					}
					return Best;
				};

				TArray<TVec3<T>> VertsA = { AllVerts[2], AllVerts[0] };
				TArray<TVec3<T>> VertsB = { TVec3<T>(0), TVec3<T>(0) };

				TArray<TEPAEntry<T>> TetFaces = InitializeEPA(VertsA, VertsB, ASupportNoXOrZ, EmptySupport);
				EXPECT_EQ(TetFaces.Num(), 0);
			}
		}
	}

	template void EPAInitTest<float>();
	
	template <typename T>
	void EPASimpleTest()
	{
		auto ZeroSupport = [](const auto& V) { return TVec3<T>(0); };

		{

			//simple box hull. 0.5 depth on x, 1 depth on y, 1 depth on z. Made z non symmetric to avoid v on tet close to 0 for this case
			TVec3<T> HullVerts[8] = { {-0.5, -1, -1}, {2, -1, -1}, {-0.5, 1, -1}, {2, 1, -1},
									  {-0.5, -1, 2}, {2, -1, 2}, {-0.5, 1, 2}, {2, 1, 2} };

			auto SupportA = [&HullVerts](const auto& V)
			{
				auto MaxDist = TNumericLimits<T>::Lowest();
				auto BestVert = HullVerts[0];
				for (const auto& Vert : HullVerts)
				{
					const auto Dist = TVec3<T>::DotProduct(V, Vert);
					if (Dist > MaxDist)
					{
						MaxDist = Dist;
						BestVert = Vert;
					}
				}
				return BestVert;
			};

			TArray <TVec3<T>> Tetrahedron = { HullVerts[0], HullVerts[2], HullVerts[3], HullVerts[4] };
			TArray <TVec3<T>> Zeros = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

			T Penetration;
			TVec3<T> Dir, WitnessA, WitnessB;
			EXPECT_EQ(EPA(Tetrahedron, Zeros, SupportA, ZeroSupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::Ok);
			EXPECT_NEAR(Penetration, 0.5, 1e-4);
			EXPECT_NEAR(Dir[0], -1, 1e-4);
			EXPECT_NEAR(Dir[1], 0, 1e-4);
			EXPECT_NEAR(Dir[2], 0, 1e-4);
			EXPECT_NEAR(WitnessA[0], -0.5, 1e-4);
			EXPECT_NEAR(WitnessA[1], 0, 1e-4);
			EXPECT_NEAR(WitnessA[2], 0, 1e-4);
			EXPECT_NEAR(WitnessB[0], 0, 1e-4);
			EXPECT_NEAR(WitnessB[1], 0, 1e-4);
			EXPECT_NEAR(WitnessB[2], 0, 1e-4);
		}

		{
			//sphere with deep penetration to make sure we have max iterations
			TSphere<T,3> Sphere(TVec3<T>(0), 10);
			auto Support = [&Sphere](const auto& V)
			{
				return Sphere.Support(V, 0);
			};

			TArray <TVec3<T>> Tetrahedron = { Support(FVec3(-1,0,0)), Support(FVec3(1,0,0)),
				Support(FVec3(0,1,0)), Support(FVec3(0,0,1))};
			TArray <TVec3<T>> Zeros = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

			T Penetration;
			TVec3<T> Dir, WitnessA, WitnessB;
			EXPECT_EQ(EPA(Tetrahedron, Zeros, Support, ZeroSupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::MaxIterations);
			EXPECT_GT(Penetration, 9);
			EXPECT_LE(Penetration, 10);
			EXPECT_GT(WitnessA.Size(), 9);	//don't know exact point, but should be 9 away from origin
			EXPECT_LE(WitnessA.Size(), 10);	//point should be interior to sphere
		}

		{
			//capsule with origin in middle
			TCapsule<T> Capsule(TVec3<T>(0, 0, 10), TVec3<T>(0, 0, -10), 3);
			auto Support = [&Capsule](const auto& V)
			{
				return Capsule.Support(V, 0);
			};

			TArray <TVec3<T>> Tetrahedron = { Support(FVec3(-1,0,0)), Support(FVec3(1,0,0)),
				Support(FVec3(0,1,0)), Support(FVec3(0,0,1)) };
			TArray <TVec3<T>> Zeros = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

			T Penetration;
			TVec3<T> Dir, WitnessA, WitnessB;
			EXPECT_EQ(EPA(Tetrahedron, Zeros, Support, ZeroSupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::Ok);
			EXPECT_NEAR(Penetration, 3, 1e-1);
			EXPECT_NEAR(Dir[2], 0, 1e-1);	//don't know direction, but it should be in xy plane
			EXPECT_NEAR(WitnessA.Size(), 3, 1e-1);	//don't know exact point, but should be 3 away from origin
		}
		{
			//capsule with origin near top
			TCapsule<T> Capsule(TVec3<T>(0, 0, -2), TVec3<T>(0, 0, -12), 3);
			auto Support = [&Capsule](const auto& V)
			{
				return Capsule.Support(V, 0);
			};

			TArray <TVec3<T>> Tetrahedron = { Support(FVec3(-1,0,0)), Support(FVec3(1,0,0)),
				Support(FVec3(0,1,0)), Support(FVec3(0,0,1)) };

			TArray <TVec3<T>> Zeros = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

			T Penetration;
			TVec3<T> Dir, WitnessA, WitnessB;
			EXPECT_EQ(EPA(Tetrahedron, Zeros, Support, ZeroSupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::Ok);
			EXPECT_NEAR(Penetration, 1, 1e-1);
			EXPECT_NEAR(Dir[0], 0, 1e-1);
			EXPECT_NEAR(Dir[1], 0, 1e-1);
			EXPECT_NEAR(Dir[2], 1, 1e-1);
			EXPECT_NEAR(WitnessA[0], 0, 1e-1);
			EXPECT_NEAR(WitnessA[1], 0, 1e-1);
			EXPECT_NEAR(WitnessA[2], 1, 1e-1);
			EXPECT_NEAR(WitnessB[0], 0, 1e-1);
			EXPECT_NEAR(WitnessB[1], 0, 1e-1);
			EXPECT_NEAR(WitnessB[2], 0, 1e-1);
		}

		{
			//box is 1,1,1 with origin in the middle to handle cases when origin is right on tetrahedron
			TVec3<T> HullVerts[8] = { {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
									  {-1, -1, 1}, {1, -1, 2}, {-1, 1, 1}, {1, 1, 1} };

			auto Support = [&HullVerts](const auto& V)
			{
				auto MaxDist = TNumericLimits<T>::Lowest();
				auto BestVert = HullVerts[0];
				for (const auto& Vert : HullVerts)
				{
					const auto Dist = TVec3<T>::DotProduct(V, Vert);
					if (Dist > MaxDist)
					{
						MaxDist = Dist;
						BestVert = Vert;
					}
				}
				return BestVert;
			};

			TArray<TVec3<T>> Tetrahedron = { HullVerts[0], HullVerts[2], HullVerts[3], HullVerts[4] };
			TArray <TVec3<T>> Zeros = { TVec3<T>(0), TVec3<T>(0), TVec3<T>(0), TVec3<T>(0) };

			T Penetration;
			TVec3<T> Dir, WitnessA, WitnessB;
			EXPECT_EQ(EPA(Tetrahedron, Zeros, Support, ZeroSupport, Penetration, Dir, WitnessA, WitnessB), EPAResult::Ok);
			EXPECT_FLOAT_EQ(Penetration, 1);
			EXPECT_NEAR(WitnessA.Size(), 1, 1e-1);	//don't know exact point, but should be 1 away from origin
		}
	}

	template void EPASimpleTest<float>();

	// Previously failing test cases that we would like to keep testing to prevent regression.
	GTEST_TEST(EPATests, EPARealFailures_Fixed)
	{
		{
			//get to EPA from GJKPenetration
			TAABB<float, 3> Box({ -50, -50, -50 }, { 50, 50, 50 });

			const FRigidTransform3 BToATM({ -8.74146843, 4.58291769, -100.029655 }, FRotation3::FromElements(6.63562241e-05, -0.000235952888, 0.00664712908, 0.999977887));
			FVec3 ClosestA, ClosestB, Normal;
			float Penetration;
			GJKPenetration(Box, Box, BToATM, Penetration, ClosestA, ClosestB, Normal);

			EXPECT_GT(Penetration, 0);
			EXPECT_LT(Penetration, 2);
		}

		// Problem: EPA was selecting the wrong face on the second box, resulting in a large penetration depth (131cm, but the box is only 20cm thick)
		// Fixed CL: 10615422
		{
			TBox<FReal, 3> A({ -12.5000000, -1.50000000, -12.5000000 }, { 12.5000000, 1.50000000, 12.5000000 });
			TBox<FReal, 3> B({ -100.000000, -100.000000, -10.0000000 }, { 100.000000, 100.000000, 10.0000000 });
			const FRigidTransform3 BToATM({ -34.9616776, 64.0135651, -10.9833698 }, FRotation3::FromElements(-0.239406615, -0.664629698, 0.637779951, 0.306901455));

			FVec3 ClosestA, ClosestB, NormalA;
			float Penetration;
			GJKPenetration(A, B, BToATM, Penetration, ClosestA, ClosestB, NormalA);
			FVec3 Normal = BToATM.InverseTransformVector(NormalA);

			EXPECT_NEAR(Penetration, 0.025f, 0.005f);
			EXPECT_NEAR(Normal.Z, -1.0f, 0.001f);
		}

		// Problem: EPA was selecting the wrong face on the second box, this was because LastEntry was initialized to first face, not best first face
		// Fixed CL: 10635151>
		{
			TBox<FReal, 3> A({ -12.5000000, -1.50000000, -12.5000000 }, { 12.5000000, 1.50000000, 12.5000000 });
			TBox<FReal, 3> B({ -100.000000, -100.000000, -10.0000000 }, { 100.000000, 100.000000, 10.0000000 });
			const FRigidTransform3 BToATM({ -50.4365005, 52.8003693, -35.1415100 }, FRotation3::FromElements(-0.112581111, -0.689017475, 0.657892346, 0.282414317));

			FVec3 ClosestA, ClosestB, NormalA;
			float Penetration;
			GJKPenetration(A, B, BToATM, Penetration, ClosestA, ClosestB, NormalA);
			FVec3 Normal = BToATM.InverseTransformVector(NormalA);

			EXPECT_LT(Penetration, 20);
			EXPECT_NEAR(Normal.Z, -1.0f, 0.001f);
		}

		// Do not know what the expected output for this test is, but it is here because it once produced NaN in the V vector in GJKRaycast2.
		// Turn on NaN diagnostics if you want to be sure to catch the failure. (Fixed now)
		{
			TArray<TPlaneConcrete<FReal, 3>> ConvexPlanes(
				{
					{{0.000000000, -1024.00000, 2.84217094e-14}, {0.000000000, -1.00000000, 0.000000000}},
					{{0.000000000, -256.000000, 8.00000000}, {0.000000000, 0.000000000, 1.00000000}},
					{{0.000000000, -1024.00000, 8.00000000}, {0.000000000, -1.00000000, 0.000000000}},
					{{0.000000000, -256.000000, 8.00000000}, {-1.00000000, -0.000000000, 0.000000000}},
					{{768.000000, -1024.00000, 2.84217094e-14}, {-0.000000000, -6.47630076e-17, -1.00000000}},
					{{0.000000000, -1024.00000, 2.84217094e-14}, {-1.00000000, 0.000000000, 0.000000000}},
					{{0.000000000, -256.000000, 8.00000000}, {0.000000000, 0.000000000, 1.00000000}},
					{{768.000000, -1024.00000, 8.00000000}, {1.00000000, -0.000000000, 0.000000000}},
					{{768.000000, -1024.00000, 2.84217094e-14}, {6.62273836e-09, 6.62273836e-09, -1.00000000}},
					{{768.000000, -448.000000, 8.00000000}, {1.00000000, 0.000000000, 0.000000000}},
					{{0.000000000, -256.000000, -2.13162821e-14}, {0.000000000, 1.00000000, 0.000000000}},
					{{0.000000000, -256.000000, 8.00000000}, {-0.000000000, 0.000000000, 1.00000000}},
					{{768.000000, -448.000000, 8.00000000}, {0.707106829, 0.707106829, 0.000000000}},
					{{576.000000, -256.000000, 3.81469727e-06}, {0.000000000, 1.00000000, -0.000000000}},
					{{768.000000, -448.000000, 8.00000000}, {0.707106829, 0.707106829, 0.000000000}},
					{{768.000000, -448.000000, 3.81469727e-06}, {6.62273836e-09, 6.62273836e-09, -1.00000000}}
				});

			TParticles<FReal, 3> SurfaceParticles(
				{
					{0.000000000, -1024.00000, 2.84217094e-14},
					{768.000000, -1024.00000, 2.84217094e-14},
					{0.000000000, -1024.00000, 8.00000000},
					{0.000000000, -256.000000, 8.00000000},
					{768.000000, -1024.00000, 8.00000000},
					{0.000000000, -256.000000, -2.13162821e-14},
					{768.000000, -448.000000, 8.00000000},
					{768.000000, -448.000000, 3.81469727e-06},
					{576.000000, -256.000000, 3.81469727e-06},
					{576.000000, -256.000000, 8.00000000}
				});

			TUniquePtr<FConvex> Convex = MakeUnique<FConvex>(MoveTemp(ConvexPlanes), MoveTemp(SurfaceParticles));
			TImplicitObjectScaled<FConvex> ScaledConvex(MakeSerializable(Convex), FVec3(1.0f), 0.0f);

			TSphere<FReal, 3> Sphere(FVec3(0.0f), 34.2120171);

			const FRigidTransform3 BToATM({ 568.001648, -535.998352, 8.00000000 }, FRotation3::FromElements(0.000000000, 0.000000000, -0.707105696, 0.707107902));
			const FVec3 LocalDir(0.000000000, 0.000000000, -1.00000000);
			const FReal Length = 384.000000;
			const FReal Thickness = 0.0;
			const bool bComputeMTD = true;
			const FVec3 Offset(-536.000000, -568.000000, -8.00000000);

			FReal OutTime = -1.0f;
			FVec3 LocalPosition(-1.0f);
			FVec3 LocalNormal(-1.0f);


			bool bResult = GJKRaycast2(ScaledConvex, Sphere, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, bComputeMTD, Offset, Thickness);

		}

	}

	// Currently broken EPA edge cases. As they are fixed move them to EPARealFailures_Fixed above so that we can ensure they don't break again.
	GTEST_TEST(EPATests, EPARealFailures_Broken)
	{

	}
}