// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestMostOpposing.h"

#include "HeadlessChaos.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Convex.h"

namespace ChaosTest
{
	using namespace Chaos;

	/*We  want to test the following:
	- Correct face index simple case
	- Correct face on shared edge
	*/

	template<class T>
	void TrimeshMostOpposing()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		TArray<uint16> DummyMaterials;
		int32 FaceIndex;

		TParticles<T, 3> Particles;
		Particles.AddParticles(6);
		Particles.X(0) = TVector<T, 3>(1, 1, 1);
		Particles.X(1) = TVector<T, 3>(5, 1, 1);
		Particles.X(2) = TVector<T, 3>(1, 5, 1);

		Particles.X(3) = TVector<T, 3>(1, 1, 1);
		Particles.X(4) = TVector<T, 3>(1, 5, 1);
		Particles.X(5) = TVector<T, 3>(1, 1, -5);

		TArray<TVector<int32, 3>> Indices;
		Indices.Emplace(0, 1, 2);
		Indices.Emplace(3, 4, 5);
		FTriangleMeshImplicitObject Tri(MoveTemp(Particles), MoveTemp(Indices), MoveTemp(DummyMaterials));

		//simple into the triangle
		bool bHit = Tri.Raycast(TVector<T, 3>(3, 2, 2), TVector<T, 3>(0, 0, -1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Tri.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 0);
		EXPECT_EQ(Tri.GetFaceNormal(0).X, Normal.X);
		EXPECT_EQ(Tri.GetFaceNormal(0).Y, Normal.Y);
		EXPECT_EQ(Tri.GetFaceNormal(0).Z, Normal.Z);

		//simple into second triangle
		bHit = Tri.Raycast(TVector<T, 3>(0, 2, 0), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, 1);
		EXPECT_EQ(Tri.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 1);
		EXPECT_EQ(Tri.GetFaceNormal(1).X, Normal.X);
		EXPECT_EQ(Tri.GetFaceNormal(1).Y, Normal.Y);
		EXPECT_EQ(Tri.GetFaceNormal(1).Z, Normal.Z);

		//very close to edge, for now just return face hit regardless of direction because that's the implementation we currently rely on.
		//todo: inconsistent with hulls, should make them the same, but may have significant impact on existing content

		bHit = Tri.Raycast(TVector<T, 3>(0, 2, 0.9), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, 1);
		EXPECT_EQ(Tri.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 1);
		EXPECT_EQ(Tri.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 1);	//ignores direction completely as per current implementation
	}

	template void TrimeshMostOpposing<float>();

	template<class T>
	void ConvexMostOpposing()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		int32 FaceIndex;

		TParticles<T, 3> Particles;
		Particles.AddParticles(6);
		Particles.X(0) = TVector<T, 3>(1, 1, 1);
		Particles.X(1) = TVector<T, 3>(5, 1, 1);
		Particles.X(2) = TVector<T, 3>(1, 5, 1);

		Particles.X(3) = TVector<T, 3>(1, 1, 1);
		Particles.X(4) = TVector<T, 3>(1, 5, 1);
		Particles.X(5) = TVector<T, 3>(1, 1, -5);

		FConvex Convex(MoveTemp(Particles));

		//simple into the triangle
		bool bHit = Convex.Raycast(TVector<T, 3>(3, 2, 2), TVector<T, 3>(0, 0, -1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_FLOAT_EQ(Position.X, 3);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 1);	//front face, just so happens that convex hull generates the planes in this order

		//simple into second triangle
		bHit = Convex.Raycast(TVector<T, 3>(0, 2, 0), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 0);
		EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);	//side face, just so happens that convex hull generates the planes in this order

		bHit = Convex.Raycast(TVector<T, 3>(0, 2, 0.99), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 1);

		//again but far enough away from edge
		bHit = Convex.Raycast(TVector<T, 3>(0, 2, 0.9), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
		EXPECT_EQ(Convex.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 3);	//too far to care about other face
	}

	template void ConvexMostOpposing<float>();

	template<class T>
	void ScaledMostOpposing()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		int32 FaceIndex;

		TParticles<T, 3> Particles;
		Particles.AddParticles(6);
		Particles.X(0) = TVector<T, 3>(0, -1, 1);
		Particles.X(1) = TVector<T, 3>(1, -1, -1);
		Particles.X(2) = TVector<T, 3>(0, 1, 1);

		Particles.X(3) = TVector<T, 3>(0, -1, 1);
		Particles.X(4) = TVector<T, 3>(0, 1, 1);
		Particles.X(5) = TVector<T, 3>(-1, -1, -1);

		TUniquePtr<FImplicitObject> Convex = MakeUnique<FConvex>(MoveTemp(Particles));

		//identity scale
		{
			TImplicitObjectScaledGeneric<T, 3> Scaled(MakeSerializable(Convex), TVector<T, 3>(1, 1, 1));

			//simple into the triangle
			bool bHit = Scaled.Raycast(TVector<T, 3>(0.5, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(Position.X, 0.5);
			EXPECT_EQ(Position.Y, 0);
			EXPECT_EQ(Position.Z, 2 - Time);
			EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 2);	//x+ face, just so happens that convex hull generates the planes in this order

			//simple into second triangle
			bHit = Scaled.Raycast(TVector<T, 3>(-2, 0, 0.5), TVector<T, 3>(1, 0, 0), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(Position.X, -2+Time);
			EXPECT_EQ(Position.Y, 0);
			EXPECT_EQ(Position.Z, 0.5);
			EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);	//x- face, just so happens that convex hull generates the planes in this order

			bHit = Scaled.Raycast(TVector<T, 3>(-0.001, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(FaceIndex, INDEX_NONE);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(-1, 0, 0), FaceIndex, 0.01), 2);

			//again but far enough away from edge
			bHit = Scaled.Raycast(TVector<T, 3>(-0.1, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(FaceIndex, INDEX_NONE);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(-1, 0, 0), FaceIndex, 0.01), 3);	//too far to care about other face
		}

		//non-uniform scale
		{
			TImplicitObjectScaledGeneric<T, 3> Scaled(MakeSerializable(Convex), TVector<T, 3>(2, 1, 1));

			//simple into the triangle
			bool bHit = Scaled.Raycast(TVector<T, 3>(0.5, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(Position.X, 0.5);
			EXPECT_EQ(Position.Y, 0);
			EXPECT_EQ(Position.Z, 2 - Time);
			EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(0, 0, -1), FaceIndex, 0.01), 2);	//x+ face, just so happens that convex hull generates the planes in this order

			//simple into second triangle
			bHit = Scaled.Raycast(TVector<T, 3>(-2, 0, 0.5), TVector<T, 3>(1, 0, 0), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(Position.X, -2 + Time);
			EXPECT_EQ(Position.Y, 0);
			EXPECT_EQ(Position.Z, 0.5);
			EXPECT_EQ(FaceIndex, INDEX_NONE);	//convex should not compute its own face index as this is too expensive
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);	//x- face, just so happens that convex hull generates the planes in this order

			bHit = Scaled.Raycast(TVector<T, 3>(-0.001, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(FaceIndex, INDEX_NONE);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(-1, 0, 0), FaceIndex, 0.01), 2);

			//again but far enough away from edge
			bHit = Scaled.Raycast(TVector<T, 3>(-0.1, 0, 2), TVector<T, 3>(0, 0, -1), 3, 0, Time, Position, Normal, FaceIndex);
			EXPECT_TRUE(bHit);
			EXPECT_EQ(FaceIndex, INDEX_NONE);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(1, 0, 0), FaceIndex, 0.01), 3);
			EXPECT_EQ(Scaled.FindMostOpposingFace(Position, TVector<T, 3>(-1, 0, 0), FaceIndex, 0.01), 3);	//too far to care about other face
		}
	}

	template void ScaledMostOpposing<float>();
}