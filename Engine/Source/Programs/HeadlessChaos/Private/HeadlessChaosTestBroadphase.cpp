// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestBroadphase.h"

#include "HeadlessChaos.h"
#include "Chaos/Box.h"
#include "Chaos/BoundingVolume.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/AABBTree.h"
#include "PBDRigidsSolver.h"
#include "Chaos/SpatialAccelerationCollection.h"

namespace ChaosTest
{
	using namespace Chaos;

	/*In general we want to test the following for each broadphase type:
	- simple intersection test as used by sim (IntersectAll)
	- ray, sweep, overlap
	- miss entire structure
	- stop mid structure
	- multi overlap
	- multi block (adjust length)
	- any
	*/

	template <typename T>
	struct TVisitor : ISpatialVisitor<int32, T>
	{
		const TGeometryParticles<T, 3>& Boxes;
		const TVector<T, 3> Start;
		const TVector<T, 3> Dir;
		TVector<T, 3> HalfExtents;
		const T Thickness;
		int32 BlockAfterN;
		bool bAny;

		TVisitor(const TVector<T,3>& InStart, const TVector<T,3>& InDir, const T InThickness, const TGeometryParticles<T,3>& InBoxes)
		: Boxes(InBoxes)
		, Start(InStart)
		, Dir(InDir)
		, HalfExtents(0)
		, Thickness(InThickness)
		, BlockAfterN(TNumericLimits<int32>::Max())
		, bAny(false)
		{}

		enum class SQType
		{
			Raycast,
			Sweep,
			Overlap
		};

		template <SQType>
		bool Visit(int32 Idx, FQueryFastData& CurData)
		{
			const TRigidTransform<T,3> BoxTM(Boxes.X(Idx), Boxes.R(Idx));
			TAABB<T, 3> Box = static_cast<const TBox<T, 3>*>(Boxes.Geometry(Idx).Get())->BoundingBox().TransformedAABB(BoxTM);
			TAABB<T, 3> ThicknedBox(Box.Min() - HalfExtents, Box.Max() + HalfExtents);

			T NewLength;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;
			int32 FaceIndex;
			const T OldLength = CurData.CurrentLength;
			if (ThicknedBox.Raycast(Start, Dir, CurData.CurrentLength, 0, NewLength, Position, Normal, FaceIndex))
			{
				Instances.Add(Idx);
				if (bAny)
				{
					return false;
				}
				if (Instances.Num() >= BlockAfterN)
				{
					//blocking so adjust Length
					CurData.SetLength(NewLength);
				}
			}
			
			return true;
		}

		bool VisitRaycast(TSpatialVisitorData<int32> Idx, FQueryFastData& CurData)
		{
			return Visit<SQType::Raycast>(Idx.Payload, CurData);
		}

		bool VisitSweep(TSpatialVisitorData<int32> Idx, FQueryFastData& CurData)
		{
			return Visit<SQType::Sweep>(Idx.Payload, CurData);
		}

		bool VisitOverlap(TSpatialVisitorData<int32> Idx)
		{
			check(false);
			return false;
		}

		virtual bool Overlap(const TSpatialVisitorData<int32>& Instance) override
		{
			return VisitOverlap(Instance);
		}

		virtual bool Raycast(const TSpatialVisitorData<int32>& Instance, FQueryFastData& CurData) override
		{
			return VisitRaycast(Instance, CurData);
		}

		virtual bool Sweep(const TSpatialVisitorData<int32>& Instance, FQueryFastData& CurData) override
		{
			return VisitSweep(Instance, CurData);
		}

		TArray<int32> Instances;
	};

	template <typename T>
	struct TOverlapVisitor : public ISpatialVisitor<int32, T>
	{
		const TGeometryParticles<T, 3>& Boxes;
		const TAABB<T, 3> Bounds;
		bool bAny;

		TOverlapVisitor(const TAABB<T,3>& InBounds, const TGeometryParticles<T, 3>& InBoxes)
			: Boxes(InBoxes)
			, Bounds(InBounds)
			, bAny(false)
		{}

		bool VisitOverlap(TSpatialVisitorData<int32> Instance)
		{
			const int32 Idx = Instance.Payload;
			const TRigidTransform<T, 3> BoxTM(Boxes.X(Idx), Boxes.R(Idx));
			TAABB<T, 3> Box = static_cast<const TBox<T, 3>*>(Boxes.Geometry(Idx).Get())->BoundingBox().TransformedAABB(BoxTM);
			
			if (Box.Intersects(Bounds))
			{
				Instances.Add(Idx);
				if (bAny)
				{
					return false;
				}
			}

			return true;
		}

		bool VisitRaycast(TSpatialVisitorData<int32> Idx, FQueryFastData&)
		{
			check(false);
			return false;
		}

		bool VisitSweep(TSpatialVisitorData<int32> Idx, FQueryFastData&)
		{
			check(false);
			return false;
		}

		virtual bool Overlap(const TSpatialVisitorData<int32>& Instance) override
		{
			return VisitOverlap(Instance);
		}

		virtual bool Raycast(const TSpatialVisitorData<int32>& Instance, FQueryFastData& CurData) override
		{
			return VisitRaycast(Instance, CurData);
		}

		virtual bool Sweep(const TSpatialVisitorData<int32>& Instance, FQueryFastData& CurData) override
		{
			return VisitSweep(Instance, CurData);
		}

		TArray<int32> Instances;
	};

	template <typename T>
	auto BuildBoxes(TUniquePtr<TBox<T,3>>& Box)
	{
		Box = MakeUnique<TBox<T, 3>>(TVector<T, 3>(0, 0, 0), TVector<T, 3>(100, 100, 100));
		auto Boxes = MakeUnique<TGeometryParticles<T, 3>>();
		const int32 NumRows = 10;
		const int32 NumCols = 10;
		const int32 NumHeight = 10;

		Boxes->AddParticles(NumRows * NumCols * NumHeight);

		int32 Idx = 0;
		for (int32 Height = 0; Height < NumHeight; ++Height)
		{
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				for (int32 Col = 0; Col < NumCols; ++Col)
				{
					Boxes->SetGeometry(Idx, MakeSerializable(Box));
					Boxes->X(Idx) = TVector<T, 3>(Col * 100, Row * 100, Height * 100);
					Boxes->R(Idx) = TRotation<T, 3>::Identity;
					Boxes->LocalBounds(Idx) = Box->BoundingBox();
					Boxes->HasBounds(Idx) = true;
					Boxes->SetWorldSpaceInflatedBounds(Idx, Box->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(Idx), Boxes->R(Idx))));
					++Idx;
				}
			}
		}

		return Boxes;
	}
	
	template <typename TSpatial, typename T>
	void SpatialTestHelper(TSpatial& Spatial, TGeometryParticles<T,3>* Boxes, TUniquePtr<TBox<T,3>>& Box, FSpatialAccelerationIdx SpatialIdx = FSpatialAccelerationIdx())
	{
		//raycast
		//miss
		{
			TVisitor<T> Visitor(TVector<T, 3>(-100, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//gather along ray
		{
			TVisitor<T> Visitor(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//gather along ray and then make modifications
		{
			auto Spatial2 = Spatial.Copy();
			TVisitor<T> Visitor(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Spatial2->Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);

			//remove from structure
			Spatial2->RemoveElementFrom(Visitor.Instances[0], SpatialIdx);

			TVisitor<T> Visitor2(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Spatial2->Raycast(Visitor2.Start, Visitor2.Dir, 1000, Visitor2);
			EXPECT_EQ(Visitor2.Instances.Num(), 9);

			//move instance away
			{
				const int32 MoveIdx = Visitor2.Instances[0];
				Boxes->X(MoveIdx) += TVector<T, 3>(1000, 0, 0);
				TAABB<T, 3> NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<T, 3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				TVisitor<T> Visitor3(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 8);

				//move instance back
				Boxes->X(MoveIdx) -= TVector<T, 3>(1000, 0, 0);
				NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<T, 3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);
			}

			//move other instance into view
			{
				const int32 MoveIdx = 5 * 5 * 5;
				const TVector<T, 3> OldPos = Boxes->X(MoveIdx);
				Boxes->X(MoveIdx) = TVector<T, 3>(0, 0, 0);
				TAABB<T, 3> NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<T, 3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				TVisitor<T> Visitor3(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 10);

				//move instance back
				Boxes->X(MoveIdx) = OldPos;
				NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<T,3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);
			}

			//move instance outside of grid bounds
			{
				const int32 MoveIdx = 5 * 5 * 5;
				const TVector<T, 3> OldPos = Boxes->X(MoveIdx);
				Boxes->X(MoveIdx) = TVector<T, 3>(-50, 0, 0);
				TAABB<T, 3> NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<T,3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				TVisitor<T> Visitor3(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 10);

				//try ray outside of bounds which should hit
				TVisitor<T> Visitor4(TVector<T, 3>(-20, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor4.Start, Visitor4.Dir, 1000, Visitor4);
				EXPECT_EQ(Visitor4.Instances.Num(), 1);

				//delete dirty instance
				Spatial2->RemoveElementFrom(MoveIdx, SpatialIdx);
				TVisitor<T> Visitor5(TVector<T, 3>(-20, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor5.Start, Visitor5.Dir, 1000, Visitor4);
				EXPECT_EQ(Visitor5.Instances.Num(), 0);

				//move instance back
				Boxes->X(MoveIdx) = OldPos;

				//create a new box
				const int32 NewIdx = Boxes->Size();
				Boxes->AddParticles(1);
				Boxes->SetGeometry(NewIdx, MakeSerializable(Box));
				Boxes->X(NewIdx) = TVector<T, 3>(-20, 0, 0);
				Boxes->R(NewIdx) = TRotation<T, 3>::Identity;
				NewBounds = Boxes->Geometry(NewIdx)->template GetObject<TBox<T,3>>()->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes->X(NewIdx), Boxes->R(NewIdx)));
				Spatial2->UpdateElementIn(NewIdx, NewBounds, true, SpatialIdx);
				TVisitor<T> Visitor6(TVector<T, 3>(-20, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor6.Start, Visitor6.Dir, 1000, Visitor6);
				EXPECT_EQ(Visitor6.Instances.Num(), 1);
			}
		}

		//stop half way through
		{
			TVisitor<T> Visitor(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 499, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 5);
		}

		//any
		{
			TVisitor<T> Visitor(TVector<T, 3>(10, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Visitor.bAny = true;
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 1);
		}

		//sweep
		//miss
		{
			TVisitor<T> Visitor(TVector<T, 3>(-100, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = TVector<T, 3>(10, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 1000, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//gather along ray
		{
			TVisitor<T> Visitor(TVector<T, 3>(-100, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = TVector<T, 3>(110, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 1000, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//stop half way through
		{
			TVisitor<T> Visitor(TVector<T, 3>(-100, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = TVector<T, 3>(110, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 499, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 5);
		}

		//right on edge and corner
		{
			TVisitor<T> Visitor(TVector<T, 3>(100, 0, 0), TVector<T, 3>(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = TVector<T, 3>(10, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 499, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//overlap
		//miss
		{
			TOverlapVisitor<T> Visitor(TAABB<T, 3>(TVector<T, 3>(-100, 0, 0), TVector<T, 3>(-10, 0, 0)), *Boxes);
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//overlap some
		{
			TOverlapVisitor<T> Visitor(TAABB<T, 3>(TVector<T, 3>(-100, 0, -10), TVector<T, 3>(110, 110, 10)), *Boxes);
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 4);
		}

		//overlap any
		{
			TOverlapVisitor<T> Visitor(TAABB<T, 3>(TVector<T, 3>(-100, 0, -10), TVector<T, 3>(110, 110, 10)), *Boxes);
			Visitor.bAny = true;
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 1);
		}
	}

	template<typename T>
	void GridBPTest()
	{
		TUniquePtr<TBox<T, 3>> Box;
		auto Boxes = BuildBoxes<T>(Box);
		TBoundingVolume<int32, T, 3> Spatial(MakeParticleView(Boxes.Get()));
		SpatialTestHelper(Spatial, Boxes.Get(), Box);
	}

	template void GridBPTest<float>();

	template<typename T>
	void GridBPTest2()
	{
		TUniquePtr<TBox<T, 3>> Box = MakeUnique<TBox<T, 3>>(TVector<T, 3>(0, 0, 0), TVector<T, 3>(100, 100, 100));
		TPBDRigidsSOAs<T, 3> SOAs;
		const int32 NumRows = 10;
		const int32 NumCols = 10;
		const int32 NumHeight = 10;

		SOAs.CreateStaticParticles(NumRows * NumCols * NumHeight);
		auto& Boxes = SOAs.GetNonDisabledStaticParticles();
		int32 Idx = 0;
		for (int32 Height = 0; Height < NumHeight; ++Height)
		{
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				for (int32 Col = 0; Col < NumCols; ++Col)
				{
					Boxes.SetGeometry(Idx, MakeSerializable(Box));
					Boxes.X(Idx) = TVector<T, 3>(Col * 100, Row * 100, Height * 100);
					Boxes.R(Idx) = TRotation<T, 3>::Identity;
					Boxes.LocalBounds(Idx) = Box->BoundingBox();
					Boxes.HasBounds(Idx) = true;
					Boxes.SetWorldSpaceInflatedBounds(Idx, Box->BoundingBox().TransformedAABB(TRigidTransform<T, 3>(Boxes.X(Idx), Boxes.R(Idx))));
					++Idx;
				}
			}
		}

		TArray<TSOAView<TGeometryParticles<T, 3>>> TmpArray = { &Boxes };
		TBoundingVolume<TGeometryParticleHandle<T,3>*, T, 3> BV(MakeParticleView(MoveTemp(TmpArray)));
		TArray<TGeometryParticleHandle<T, 3>*> Handles = BV.FindAllIntersections(TAABB<T, 3>(TVector<T, 3>(0), TVector<T, 3>(10)));
		EXPECT_EQ(Handles.Num(), 1);
		EXPECT_EQ(Handles[0], Boxes.Handle(0));

		Handles = BV.FindAllIntersections(TAABB<T, 3>(TVector<T, 3>(0), TVector<T, 3>(0, 0, 110)));
		EXPECT_EQ(Handles.Num(), 2);

		//create BV with an array of handles instead (useful for partial structures)
		{
			TBoundingVolume<TGeometryParticleHandle<T,3>*, T, 3> BV2(MakeHandleView(Handles));
			TArray<TGeometryParticleHandle<T, 3>*> Handles2 = BV2.FindAllIntersections(TAABB<T, 3>(TVector<T, 3>(0), TVector<T, 3>(10)));
			EXPECT_EQ(Handles2.Num(), 1);
			EXPECT_EQ(Handles2[0], Boxes.Handle(0));

			Handles2 = BV2.FindAllIntersections(TAABB<T, 3>(TVector<T, 3>(0), TVector<T, 3>(0, 0, 110)));
			EXPECT_EQ(Handles2.Num(), 2);
		}
	}

	template void GridBPTest2<float>();

	template <typename T>
	void AABBTreeTest()
	{
		{
			TUniquePtr<TBox<T, 3>> Box;
			auto Boxes = BuildBoxes<T>(Box);
			TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T> Spatial(MakeParticleView(Boxes.Get()));

			while (!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			SpatialTestHelper(Spatial, Boxes.Get(), Box);
		}

		{
			TUniquePtr<TBox<T, 3>> Box;
			auto Boxes = BuildBoxes<T>(Box);
			TAABBTree<int32, TBoundingVolume<int32, T, 3>, T> Spatial(MakeParticleView(Boxes.Get()));

			while (!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			SpatialTestHelper(Spatial, Boxes.Get(), Box);
		}

		{
			//too many boxes so reoptimize
			TUniquePtr<TBox<T,3>> Box;
			auto Boxes = BuildBoxes<T>(Box);
			TAABBTree<int32,TBoundingVolume<int32,T,3>,T> Spatial(MakeParticleView(Boxes.Get()));

			while(!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			EXPECT_EQ(Spatial.NumDirtyElements(),0);

			//fill up until dirty limit
			int32 Count;
			for(Count = 0; Count < 10; ++Count)
			{
				auto Boxes2 = BuildBoxes<T>(Box);
				for(uint32 Idx = 0; Idx < Boxes2->Size(); ++Idx)
				{
					Spatial.UpdateElement(Idx + Boxes->Size() * Count,Boxes2->WorldSpaceInflatedBounds(Idx),true);
				}

				EXPECT_EQ(Spatial.NumDirtyElements(), (Count+1)*Boxes->Size());
			}

			//finally pass dirty limit so reset to 0 and then add the remaining new boxes
			auto Boxes2 = BuildBoxes<T>(Box);
			for(uint32 Idx = 0; Idx < Boxes2->Size(); ++Idx)
			{
				Spatial.UpdateElement(Idx + Boxes->Size() * Count,Boxes2->WorldSpaceInflatedBounds(Idx),true);
			}

			EXPECT_EQ(Spatial.NumDirtyElements(),Boxes->Size() - 1);
		}
	}
	template void AABBTreeTest<float>();


	template <typename T>
	void AABBTreeTimesliceTest()
	{
		TUniquePtr<TBox<T, 3>> Box;
		auto Boxes = BuildBoxes<T>(Box);

		// build AABB in one go
		TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T> SpatialBuildImmediate(
			MakeParticleView(Boxes.Get()) 
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxChildrenInLeaf 
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxTreeDepth
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxPayloadBounds
			, 0); // build entire tree in one go, no timeslicing

		EXPECT_TRUE(SpatialBuildImmediate.IsAsyncTimeSlicingComplete());
		
		// build AABB in time-sliced sections
		TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T> SpatialTimesliced(
			MakeParticleView(Boxes.Get())
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxChildrenInLeaf
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxTreeDepth
			, TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>::DefaultMaxPayloadBounds
			, 20); // build in small iteration steps, 20 iterations per call to ProgressAsyncTimeSlicing
		EXPECT_FALSE(SpatialTimesliced.IsAsyncTimeSlicingComplete());

		while (!SpatialTimesliced.IsAsyncTimeSlicingComplete())
		{
			SpatialTimesliced.ProgressAsyncTimeSlicing(false);
		}	

		// now check both AABBs have the same hierarchy
		// (indices will be different but walking tree should give same results)

		TAABB<T, 3> Tmp = TAABB<T, 3>::ZeroAABB();

		TArray<TAABB<T, 3>> AllBoundsBuildImmediate;
		SpatialBuildImmediate.GetAsBoundsArray(AllBoundsBuildImmediate, 0, -1, Tmp);

		TArray<TAABB<T, 3>> AllBoundsTimesliced;
		SpatialTimesliced.GetAsBoundsArray(AllBoundsTimesliced, 0, -1, Tmp);

		EXPECT_EQ(AllBoundsBuildImmediate.Num(), AllBoundsTimesliced.Num());

		for (int i=0; i<AllBoundsBuildImmediate.Num(); i++)
		{
			EXPECT_EQ(AllBoundsBuildImmediate[i].Center(), AllBoundsTimesliced[i].Center());
			EXPECT_EQ(AllBoundsBuildImmediate[i].Extents(), AllBoundsTimesliced[i].Extents());
		}
	}
	template void AABBTreeTimesliceTest<float>();

	template <typename T>
	void BroadphaseCollectionTest()
	{
		using TreeType = TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>;
		{
			TUniquePtr<TBox<T, 3>> Box;
			auto Boxes = BuildBoxes<T>(Box);
			auto Spatial = MakeUnique<TreeType>(MakeParticleView(Boxes.Get()));

			while (!Spatial->IsAsyncTimeSlicingComplete())
			{
				Spatial->ProgressAsyncTimeSlicing(false);
			}

			TSpatialAccelerationCollection<TreeType> AccelerationCollection;
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial), 0);
			FSpatialAccelerationIdx SpatialIdx = { 0,0 };
			SpatialTestHelper(AccelerationCollection, Boxes.Get(), Box, SpatialIdx);
		}

		{
			using BVType = TBoundingVolume<int32, T, 3>;
			TUniquePtr<TBox<T, 3>> Box;
			auto Boxes0 = BuildBoxes<T>(Box);
			auto Spatial0 = MakeUnique<TreeType>(MakeParticleView(Boxes0.Get()));
			while (!Spatial0->IsAsyncTimeSlicingComplete())
			{
				Spatial0->ProgressAsyncTimeSlicing(false);
			}

			TGeometryParticles<T, 3> EmptyBoxes;
			auto Spatial1 = MakeUnique<BVType>(MakeParticleView(&EmptyBoxes));
			while (!Spatial1->IsAsyncTimeSlicingComplete())
			{
				Spatial1->ProgressAsyncTimeSlicing(false);
			}

			TSpatialAccelerationCollection<TreeType, BVType> AccelerationCollection;
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial0), 0);
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial1), 1);

			FSpatialAccelerationIdx SpatialIdx = { 0,0 };
			SpatialTestHelper(AccelerationCollection, Boxes0.Get(), Box, SpatialIdx);
		}

		{
			using BVType = TBoundingVolume<int32, T, 3>;
			TUniquePtr<TBox<T, 3>> Box;
			auto Boxes1 = BuildBoxes<T>(Box);
			TGeometryParticles<T, 3> EmptyBoxes;

			auto Spatial0 = MakeUnique<TreeType>(MakeParticleView(&EmptyBoxes));
			auto Spatial1 = MakeUnique<BVType>(MakeParticleView(Boxes1.Get()));

			TSpatialAccelerationCollection<TreeType, BVType> AccelerationCollection;
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial0), 0);
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial1), 1);

			FSpatialAccelerationIdx SpatialIdx = { 1,0 };
			SpatialTestHelper(AccelerationCollection, Boxes1.Get(), Box, SpatialIdx);
		}
	}
	template void BroadphaseCollectionTest<float>();

	void TestPendingSpatialDataHandlePointerConflict()
	{
		// We are testing an edge case in the way we queue updates to our acceleration structures.
		// Particle A is deleted and is pending removal from accel structure
		// Particle B is allocated, coincidentally having the same handle pointer as Particle A
		// Particle B is pending an insertion into structure
		// Particle B is deleted 
		// Queue is flushed, we are testing that Particle A was removed successfully.

		// If this test fails, it means Particle B is probably stomping on data related to Particle A's removal.


		TPBDRigidsSOAs<FReal, 3> Particles;
		TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles = Particles.CreateDynamicParticles(1);

		auto Particle = ParticleHandles[0];
		Particle->GTGeometryParticle() = TGeometryParticle<FReal, 3>::CreateParticle().Release();
		Particle->X() = TVector<FReal, 3>(0, 0, 0);

		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);

		// Flush spatial acceleration structures to put particle into structure.
		Evolution.FlushSpatialAcceleration();

		// Confirm particle is returned via query.
		{
			auto SpatialAccelerationCollection = Evolution.GetSpatialAcceleration();
			TArray<FSpatialAccelerationIdx> SpatialIndices = SpatialAccelerationCollection->GetAllSpatialIndices();
			TAABB<FReal, 3> QueryBounds(FVec3(-1, -1, -1), FVec3(1, 1, 1));
			uint32 HitNum = 0;
			for (FSpatialAccelerationIdx SpatialIdx : SpatialIndices)
			{
				auto SpatialAcceleration = SpatialAccelerationCollection->GetSubstructure(SpatialIdx);
				TArray<TAccelerationStructureHandle<FReal, 3>> Hits = SpatialAcceleration->FindAllIntersections(QueryBounds);
				HitNum += Hits.Num();
			}

			EXPECT_EQ(HitNum, 1);
		}


		// Removal from accel structure is now pending (not completely applied until flush)
		Evolution.RemoveParticleFromAccelerationStructure(*Particle);


		// Assign new game thread particle to handle (this simulates Particle B allocation, with reused Handle pointer)
		Particle->GTGeometryParticle() = TGeometryParticle<FReal, 3>::CreateParticle().Release();

		// Remove new Particle from accel structure
		Evolution.RemoveParticleFromAccelerationStructure(*Particle);

		// Flush changes, this should remove first particle successfully.
		Evolution.FlushSpatialAcceleration();

		// Confirm particle is not in internal structure by performing same query.
		{
			auto SpatialAccelerationCollection = Evolution.GetSpatialAcceleration();
			TArray<FSpatialAccelerationIdx> SpatialIndices = SpatialAccelerationCollection->GetAllSpatialIndices();
			TAABB<FReal, 3> QueryBounds(FVec3(-1, -1, -1), FVec3(1, 1, 1));
			uint32 HitNum = 0;
			for (FSpatialAccelerationIdx SpatialIdx : SpatialIndices)
			{
				auto SpatialAcceleration = SpatialAccelerationCollection->GetSubstructure(SpatialIdx);
				TArray<TAccelerationStructureHandle<FReal, 3>> Hits = SpatialAcceleration->FindAllIntersections(QueryBounds);
				HitNum += Hits.Num();
			}

			EXPECT_EQ(HitNum, 0);
		}

		// Confirm particle is not in external structure by swapping and testing.
		{
			using AABBTreeType = TAABBTree<TAccelerationStructureHandle<FReal, 3>, TAABBTreeLeafArray<TAccelerationStructureHandle<FReal, 3>, FReal>, FReal>;
			TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>> ExternalStructure = TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>(new TSpatialAccelerationCollection<AABBTreeType>());
			Evolution.UpdateExternalAccelerationStructure(ExternalStructure);

			ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>& SpatialAccelerationCollection = *ExternalStructure.Get();
			TArray<FSpatialAccelerationIdx> SpatialIndices = SpatialAccelerationCollection.GetAllSpatialIndices();
			TAABB<FReal, 3> QueryBounds(FVec3(-1, -1, -1), FVec3(1, 1, 1));
			uint32 HitNum = 0;
			for (FSpatialAccelerationIdx SpatialIdx : SpatialIndices)
			{
				auto SpatialAcceleration = SpatialAccelerationCollection.GetSubstructure(SpatialIdx);
				TArray<TAccelerationStructureHandle<FReal, 3>> Hits = SpatialAcceleration->FindAllIntersections(QueryBounds);
				HitNum += Hits.Num();
			}

			EXPECT_EQ(HitNum, 0);
		}
	}
}