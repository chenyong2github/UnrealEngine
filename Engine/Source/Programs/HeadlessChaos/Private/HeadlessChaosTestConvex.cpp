// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestEPA.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/Core.h"
#include "Chaos/GJK.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Particles.h"

namespace ChaosTest
{
	using namespace Chaos;

	void TestConvexBuilder_Box_FaceMerge(const FVec3* Vertices, const int32 NumVertices)
	{
		TParticles<FReal, 3> Particles;
		Particles.AddParticles(NumVertices);
		for (int32 ParticleIndex = 0; ParticleIndex < NumVertices; ++ParticleIndex)
		{
			Particles.X(ParticleIndex) = Vertices[ParticleIndex];
		}

		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TArray<TArray<int32>> FaceVertices;
		TParticles<FReal, 3> SurfaceParticles;
		TAABB<FReal, 3> LocalBounds;

		FConvexBuilder::Build(Particles, Planes, FaceVertices, SurfaceParticles, LocalBounds);
		FConvexBuilder::MergeFaces(Planes, FaceVertices, SurfaceParticles);

		// Check that we have the right number of faces and particles
		EXPECT_EQ(SurfaceParticles.Size(), 8);
		EXPECT_EQ(Planes.Num(), 6);
		EXPECT_EQ(FaceVertices.Num(), 6);

		// Make sure the verts are correct and agree on the normal
		for (int32 FaceIndex = 0; FaceIndex < FaceVertices.Num(); ++FaceIndex)
		{
			EXPECT_EQ(FaceVertices[FaceIndex].Num(), 4);
			for (int32 VertexIndex0 = 0; VertexIndex0 < FaceVertices[FaceIndex].Num(); ++VertexIndex0)
			{
				int32 VertexIndex1 = Chaos::Utilities::WrapIndex(VertexIndex0 + 1, 0, FaceVertices[FaceIndex].Num());
				int32 VertexIndex2 = Chaos::Utilities::WrapIndex(VertexIndex0 + 2, 0, FaceVertices[FaceIndex].Num());
				const FVec3 Vertex0 = SurfaceParticles.X(FaceVertices[FaceIndex][VertexIndex0]);
				const FVec3 Vertex1 = SurfaceParticles.X(FaceVertices[FaceIndex][VertexIndex1]);
				const FVec3 Vertex2 = SurfaceParticles.X(FaceVertices[FaceIndex][VertexIndex2]);

				// All vertices should lie in a plane at the same distance
				const FReal Dist0 = FVec3::DotProduct(Vertex0, Planes[FaceIndex].Normal());
				const FReal Dist1 = FVec3::DotProduct(Vertex1, Planes[FaceIndex].Normal());
				const FReal Dist2 = FVec3::DotProduct(Vertex2, Planes[FaceIndex].Normal());
				EXPECT_NEAR(Dist0, 50.0f, 1.e-3f);
				EXPECT_NEAR(Dist1, 50.0f, 1.e-3f);
				EXPECT_NEAR(Dist2, 50.0f, 1.e-3f);

				// All sequential edge pairs should agree on winding
				const FReal Winding = FVec3::DotProduct(FVec3::CrossProduct(Vertex1 - Vertex0, Vertex2 - Vertex1), Planes[FaceIndex].Normal());
				EXPECT_GT(Winding, 0.0f);
			}
		}
	}

	GTEST_TEST(ConvexBuilderTests, TestBoxFaceMergeSimple)
	{
		const FVec3 Vertices[] =
		{
			FVec3(-50,		-50,	-50),
			FVec3(-50,		-50,	50),
			FVec3(-50,		50,		-50),
			FVec3(-50,		50,		50),
			FVec3(50,		-50,	-50),
			FVec3(50,		-50,	50),
			FVec3(50,		50,		-50),
			FVec3(50,		50,		50),
		};

		TestConvexBuilder_Box_FaceMerge(Vertices, UE_ARRAY_COUNT(Vertices));
	}


	void TestConvex_StructrureData(const FVec3* Vertices, const int32 NumVertices)
	{
		TParticles<FReal, 3> Particles;
		Particles.AddParticles(NumVertices);
		for (int32 ParticleIndex = 0; ParticleIndex < NumVertices; ++ParticleIndex)
		{
			Particles.X(ParticleIndex) = Vertices[ParticleIndex];
		}

		FConvex Convex(Particles, 0.0f);

		// Check all per-plane data
		for (int32 PlaneIndex = 0; PlaneIndex < Convex.GetFaces().Num(); ++PlaneIndex)
		{
			// All vertices should be on the plane
			for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < (int32)Convex.GetPlaneVertices(PlaneIndex).Num(); ++PlaneVertexIndex)
			{
				const int32 VertexIndex = Convex.GetPlaneVertices(PlaneIndex)[PlaneVertexIndex];
				const FReal VertexDistance = FVec3::DotProduct(Convex.GetPlane(PlaneIndex).Normal(), Convex.GetVertex(VertexIndex) - Convex.GetPlane(PlaneIndex).X());
				EXPECT_NEAR(VertexDistance, 0.0f, 1.e-3f);
			}
		}

		// Check all per-vertex data
		for (int32 VertexIndex = 0; VertexIndex < (int32)Convex.GetSurfaceParticles().Size(); ++VertexIndex)
		{
			// All planes should pass through the vertex
			for (int32 VertexPlaneIndex = 0; VertexPlaneIndex < Convex.GetVertexPlanes(VertexIndex).Num(); ++VertexPlaneIndex)
			{
				const int32 PlaneIndex = Convex.GetVertexPlanes(VertexIndex)[VertexPlaneIndex];
				const FReal VertexDistance = FVec3::DotProduct(Convex.GetPlane(PlaneIndex).Normal(), Convex.GetVertex(VertexIndex) - Convex.GetPlane(PlaneIndex).X());
				EXPECT_NEAR(VertexDistance, 0.0f, 1.e-3f);
			}
		}
	}

	GTEST_TEST(ConvexTests, TestBoxStructureData)
	{
		const FVec3 Vertices[] =
		{
			FVec3(-50,		-50,	-50),
			FVec3(-50,		-50,	50),
			FVec3(-50,		50,		-50),
			FVec3(-50,		50,		50),
			FVec3(50,		-50,	-50),
			FVec3(50,		-50,	50),
			FVec3(50,		50,		-50),
			FVec3(50,		50,		50),
		};

		TestConvex_StructrureData(Vertices, UE_ARRAY_COUNT(Vertices));
	}

	GTEST_TEST(ConvexTests, TestBoxStructureData2)
	{
		const FVec3 Vertices[] =
		{
			FVec3(0, 0, 12.0f),
			FVec3(-0.707f, -0.707f, 10.0f),
			FVec3(0, -1, 10.0f),
			FVec3(0.707f, -0.707f, 10.0f),
			FVec3(1, 0, 10.0f),
			FVec3(0.707f, 0.707f, 10.0f),
			FVec3(0.0f, 1.0f, 10.0f),
			FVec3(-0.707f, 0.707f, 10.0f),
			FVec3(-1.0f, 0.0f, 10.0f),
			FVec3(-0.707f, -0.707f, 0.0f),
			FVec3(0, -1, 0.0f),
			FVec3(0.707f, -0.707f, 0.0f),
			FVec3(1, 0, 0.0f),
			FVec3(0.707f, 0.707f, 0.0f),
			FVec3(0.0f, 1.0f, 0.0f),
			FVec3(-0.707f, 0.707f, 0.0f),
			FVec3(-1.0f, 0.0f, 0.0f),
			FVec3(0, 0, -2.0f),
		};

		TestConvex_StructrureData(Vertices, UE_ARRAY_COUNT(Vertices));
	}
}