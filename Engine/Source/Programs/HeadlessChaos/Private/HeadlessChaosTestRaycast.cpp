// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestRaycast.h"

#include "HeadlessChaos.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace ChaosTest
{
	using namespace Chaos;

	/*In general we want to test the following for each geometry type:
	- time represents how far a swept object travels
	- position represents the world position where an intersection first occurred. If multiple first intersections we should do something well defined (what?)
	- normal represents the world normal where an intersection first occurred.
	- time vs position (i.e. in a thick raycast we want point of impact)
	- initial overlap blocks
	- near hit
	- near miss
	*/

	template<class T>
	void SphereRaycast()
	{
		TSphere<T,3> Sphere(TVector<T, 3>(1), 15);

		T Time;
		TVector<T, 3> Position, Normal;
		int32 FaceIndex;

		//simple
		bool bHit = Sphere.Raycast(TVector<T,3>(1,1,17), TVector<T, 3>(0, 0, -1), 30, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, (T)1);
		
		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 16);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		//initial overlap
		bHit = Sphere.Raycast(TVector<T, 3>(1, 1, 14), TVector<T, 3>(0, 0, -1), 15, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 0);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		//near hit
		bHit = Sphere.Raycast(TVector<T, 3>(16, 1, 16), TVector<T, 3>(0, 0, -1), 30, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 15);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Position.X, 16);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//near miss
		bHit = Sphere.Raycast(TVector<T, 3>(16 + 1e-4, 1, 16), TVector<T, 3>(0, 0, -1), 30, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//time vs position
		bHit = Sphere.Raycast(TVector<T, 3>(21, 1, 16), TVector<T, 3>(0, 0, -1), 30, 5, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 15);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 16);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//passed miss
		bHit = Sphere.Raycast(TVector<T, 3>(1, 1, -14 - 1e-4), TVector<T, 3>(0, 0, -1), 30, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);
	}

	template<class T>
	void PlaneRaycast()
	{
		TPlane<T, 3> Plane(TVector<T, 3>(1), TVector<T, 3>(1, 0, 0));

		T Time;
		TVector<T, 3> Position, Normal;
		int32 FaceIndex;

		//simple
		bool bHit = Plane.Raycast(TVector<T, 3>(2, 1, 1), TVector<T, 3>(-1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//Other side of plane
		bHit = Plane.Raycast(TVector<T, 3>(-1, 1, 1), TVector<T, 3>(1, 0, 0), 4, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 2);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, -1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//initial overlap
		bHit = Plane.Raycast(TVector<T, 3>(2, 1, 1), TVector<T, 3>(1, 0, 0), 2, 3, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 0);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		//near hit
		bHit = Plane.Raycast(TVector<T, 3>(1+1, 1, 1), TVector<T, 3>(-1e-2, 0, 1).GetUnsafeNormal(), 100.01, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 101);

		//near miss
		bHit = Plane.Raycast(TVector<T, 3>(1 + 1, 1, 1), TVector<T, 3>(-1e-2, 0, 1).GetUnsafeNormal(), 99.9, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//time vs position
		bHit = Plane.Raycast(TVector<T, 3>(-1, 1, 1), TVector<T, 3>(1, 0, 0), 4, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);
	}

	template<class T>
	void CapsuleRaycast()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		int32 FaceIndex;

		//straight down
		TCapsule<T> Capsule(TVector<T, 3>(1, 1, 1), TVector<T, 3>(1, 1, 9), 1);
		bool bHit = Capsule.Raycast(TVector<T, 3>(1, 1, 11), TVector<T, 3>(0, 0, -1), 2, 0, Time, Position, Normal, FaceIndex);
		
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 10);

		//straight up
		bHit = Capsule.Raycast(TVector<T, 3>(1, 1, -1), TVector<T, 3>(0, 0, 1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, -1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 0);

		//cylinder
		bHit = Capsule.Raycast(TVector<T, 3>(3, 1, 7), TVector<T, 3>(-1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);

		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 7);

		//cylinder away
		bHit = Capsule.Raycast(TVector<T, 3>(3, 1, 7), TVector<T, 3>(1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);
		
		// initial overlap cap
		bHit = Capsule.Raycast(TVector<T, 3>(1, 1, 9.5), TVector<T, 3>(-1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Time, 0);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		// initial overlap cylinder
		bHit = Capsule.Raycast(TVector<T, 3>(1, 1, 7), TVector<T, 3>(-1, 0, 0), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Time, 0);

		//cylinder time vs position
		bHit = Capsule.Raycast(TVector<T, 3>(4, 1, 7), TVector<T, 3>(-1, 0, 0), 4, 1, Time, Position, Normal, FaceIndex);

		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 7);

		//normal independent of ray dir
		bHit = Capsule.Raycast(TVector<T, 3>(4, 1, 7), TVector<T, 3>(-1, 0, -1).GetUnsafeNormal(), 4, 1, Time, Position, Normal, FaceIndex);

		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 2);

		//near hit orthogonal
		bHit = Capsule.Raycast(TVector<T, 3>(2, 3, 7), TVector<T, 3>(0, -1, 0), 4, 0, Time, Position, Normal, FaceIndex);

		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 2);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 7);

		//near miss
		bHit = Capsule.Raycast(TVector<T, 3>(2 + 1e-4, 3, 7), TVector<T, 3>(0, -1, 0), 4, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//near hit straight down
		bHit = Capsule.Raycast(TVector<T, 3>(0, 1, 11), TVector<T, 3>(0, 0, -1), 20, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, -1);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 0);

		EXPECT_FLOAT_EQ(Position.X, 0);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 9);

		bHit = Capsule.Raycast(TVector<T, 3>(-1e-4, 1, 11), TVector<T, 3>(0, 0, -1), 20, 0, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);
	}

	template <typename T>
	void TriangleRaycast()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		TArray<uint16> DummyMaterials;
		int32 FaceIndex;

		TParticles<T, 3> Particles;
		Particles.AddParticles(3);
		Particles.X(0) = TVector<T, 3>(1, 1, 1);
		Particles.X(1) = TVector<T, 3>(5, 1, 1);
		Particles.X(2) = TVector<T, 3>(1, 5, 1);
		TArray<TVector<int32, 3>> Indices;
		Indices.Emplace(0, 1, 2);
		FTriangleMeshImplicitObject Tri(MoveTemp(Particles), MoveTemp(Indices), MoveTemp(DummyMaterials));

		//simple into the triangle
		bool bHit = Tri.Raycast(TVector<T, 3>(3, 2, 2), TVector<T, 3>(0, 0, -1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, 0);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 3);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//double sided
		bHit = Tri.Raycast(TVector<T, 3>(3, 2, 0), TVector<T, 3>(0, 0, 1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, 0);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, -1);

		EXPECT_FLOAT_EQ(Position.X, 3);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//time vs position
		bHit = Tri.Raycast(TVector<T, 3>(3, 2, 3), TVector<T, 3>(0, 0, -1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, 0);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 3);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//initial miss, border hit
		bHit = Tri.Raycast(TVector<T, 3>(0.5, 2, 3), TVector<T, 3>(0, 0, -1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, 0);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//initial overlap with plane, but miss triangle
		bHit = Tri.Raycast(TVector<T, 3>(10, 1, 1), TVector<T, 3>(0, 0, -1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//parallel with triangle
		bHit = Tri.Raycast(TVector<T, 3>(-1, 1, 1), TVector<T, 3>(1, 0, 0), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, 0);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);
	}

	template <typename T>
	void BoxRaycast()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		int32 FaceIndex;

		TAABB<T, 3> Box(TVector<T, 3>(1, 1, 1), TVector<T, 3>(3, 5, 3));
		
		//simple into the box
		bool bHit = Box.Raycast(TVector<T, 3>(2, 3, 4), TVector<T, 3>(0, 0, -1), 2, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 3);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		//time vs position
		bHit = Box.Raycast(TVector<T, 3>(2, 3, 5), TVector<T, 3>(0, 0, -1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 3);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		//edge
		bHit = Box.Raycast(TVector<T, 3>(0.5, 2, -1), TVector<T, 3>(0, 0, 1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);
				
		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//corner
		bHit = Box.Raycast(TVector<T, 3>(0.5, 1, -1), TVector<T, 3>(0, 0, 1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//near hit by corner edge
		const TVector<T, 3> StartEmptyRegion(1 - FMath::Sqrt(2) / 2, 1 - FMath::Sqrt(2) / 2, -1);
		bHit = Box.Raycast(StartEmptyRegion, TVector<T, 3>(0, 0, 1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 1);

		//near miss by corner edge
		const TVector<T, 3> StartEmptyRegionMiss(StartEmptyRegion[0] - 1e-4, StartEmptyRegion[1] - 1e-4, StartEmptyRegion[2]);
		bHit = Box.Raycast(StartEmptyRegionMiss, TVector<T, 3>(0, 0, 1), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//start in corner voronoi but end in edge voronoi
		bHit = Box.Raycast(TVector<T,3>(0,0, 0.8), TVector<T, 3>(1, 1, 5).GetUnsafeNormal(), 2, 1, Time, Position, Normal, FaceIndex);

		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_GT(Position.Z, 1);

		//start in voronoi and miss
		bHit = Box.Raycast(TVector<T, 3>(0, 0, 0.8), TVector<T, 3>(-1, -1, 0).GetUnsafeNormal(), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_FALSE(bHit);

		//initial overlap
		bHit = Box.Raycast(TVector<T, 3>(1, 1, 2), TVector<T, 3>(-1, -1, 0).GetUnsafeNormal(), 2, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_EQ(FaceIndex, INDEX_NONE);
		EXPECT_EQ(Time, 0);
	}

	template <typename T>
	void ScaledRaycast()
	{
		T Time;
		TVector<T, 3> Position;
		TVector<T, 3> Normal;
		int32 FaceIndex;
		const T Thickness = 0.1;

		TUniquePtr<TSphere<T, 3>> Sphere = MakeUnique<TSphere<T,3>>(TVector<T, 3>(1), 2);
		TImplicitObjectScaled<TSphere<T, 3>> Unscaled(MakeSerializable(Sphere), TVector<T,3>(1));
		TImplicitObjectScaled<TSphere<T, 3>> UnscaledThickened(MakeSerializable(Sphere), TVector<T, 3>(1), Thickness);
		TImplicitObjectScaled<TSphere<T, 3>> UniformScaled(MakeSerializable(Sphere), TVector<T,3>(2));
		TImplicitObjectScaled<TSphere<T, 3>> UniformScaledThickened(MakeSerializable(Sphere), TVector<T, 3>(2), Thickness);
		TImplicitObjectScaled<TSphere<T, 3>> NonUniformScaled(MakeSerializable(Sphere), TVector<T,3>(2,1,1));
		TImplicitObjectScaled<TSphere<T, 3>> NonUniformScaledThickened(MakeSerializable(Sphere), TVector<T, 3>(2, 1, 1), Thickness);

		//simple
		bool bHit = Unscaled.Raycast(TVector<T, 3>(1, 1, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 5);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		bHit = UnscaledThickened.Raycast(TVector<T, 3>(1, 1, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 5 - Thickness);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 1);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		bHit = UniformScaled.Raycast(TVector<T, 3>(2, 2, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 2);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 6);

		bHit = UniformScaledThickened.Raycast(TVector<T, 3>(2, 2, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 2 - Thickness * 2);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 6);

		bHit = NonUniformScaled.Raycast(TVector<T, 3>(2, 1, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 5);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		bHit = NonUniformScaledThickened.Raycast(TVector<T, 3>(2, 1, 8), TVector<T, 3>(0, 0, -1), 8, 0, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 5 - Thickness);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 1);
		EXPECT_FLOAT_EQ(Position.Z, 3);

		//scaled thickness
		bHit = UniformScaled.Raycast(TVector<T, 3>(2, 2, 8), TVector<T, 3>(0, 0, -1), 8, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 6);

		bHit = UniformScaledThickened.Raycast(TVector<T, 3>(2, 2, 8), TVector<T, 3>(0, 0, -1), 8, 1, Time, Position, Normal, FaceIndex);
		EXPECT_TRUE(bHit);
		EXPECT_FLOAT_EQ(Time, 1 - Thickness * 2);
		EXPECT_EQ(FaceIndex, INDEX_NONE);

		EXPECT_FLOAT_EQ(Normal.X, 0);
		EXPECT_FLOAT_EQ(Normal.Y, 0);
		EXPECT_FLOAT_EQ(Normal.Z, 1);

		EXPECT_FLOAT_EQ(Position.X, 2);
		EXPECT_FLOAT_EQ(Position.Y, 2);
		EXPECT_FLOAT_EQ(Position.Z, 6);
	}

	template void SphereRaycast<float>();
	template void PlaneRaycast<float>();
	template void CapsuleRaycast<float>();
	template void TriangleRaycast<float>();
	template void BoxRaycast<float>();
	template void ScaledRaycast<float>();
}