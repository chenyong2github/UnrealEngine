// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestBroadphase.h"

#include "HeadlessChaos.h"
#include "Chaos/Box.h"
#include "Chaos/BoundingVolume.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/AABBTree.h"
#include "ChaosLog.h"
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

	struct FVisitor : ISpatialVisitor<int32>
	{
		const FGeometryParticles& Boxes;
		const FVec3 Start;
		const FVec3 Dir;
		FVec3 HalfExtents;
		const FReal Thickness;
		int32 BlockAfterN;
		bool bAny;

		FVisitor(const FVec3& InStart, const FVec3& InDir, const FReal InThickness, const FGeometryParticles& InBoxes)
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
			const FRigidTransform3 BoxTM(Boxes.X(Idx), Boxes.R(Idx));
			FAABB3 Box = static_cast<const TBox<FReal, 3>*>(Boxes.Geometry(Idx).Get())->BoundingBox().TransformedAABB(BoxTM);
			FAABB3 ThicknedBox(Box.Min() - HalfExtents, Box.Max() + HalfExtents);

			FReal NewLength;
			FVec3 Position;
			FVec3 Normal;
			int32 FaceIndex;
			const FReal OldLength = CurData.CurrentLength;
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

	struct FOverlapVisitor : public ISpatialVisitor<int32>
	{
		const FGeometryParticles& Boxes;
		const FAABB3 Bounds;
		bool bAny;

		FOverlapVisitor(const FAABB3& InBounds, const FGeometryParticles& InBoxes)
			: Boxes(InBoxes)
			, Bounds(InBounds)
			, bAny(false)
		{}

		bool VisitOverlap(TSpatialVisitorData<int32> Instance)
		{
			const int32 Idx = Instance.Payload;
			const FRigidTransform3 BoxTM(Boxes.X(Idx), Boxes.R(Idx));
			FAABB3 Box = static_cast<const TBox<FReal, 3>*>(Boxes.Geometry(Idx).Get())->BoundingBox().TransformedAABB(BoxTM);
			
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

	struct FStressTestVisitor : ISpatialVisitor<FAccelerationStructureHandle>
	{
		using FPayload = FAccelerationStructureHandle;

		FStressTestVisitor() {}

		enum class SQType
		{
			Raycast,
			Sweep,
			Overlap
		};

		bool VisitRaycast(const TSpatialVisitorData<FPayload>& Data, FQueryFastData& CurData)
		{
			return true;
		}

		bool VisitSweep(const TSpatialVisitorData<FPayload>& Data, FQueryFastData& CurData)
		{
			return true;
		}

		bool VisitOverlap(const TSpatialVisitorData<FPayload>& Data)
		{
			return true;
		}

		virtual bool Overlap(const TSpatialVisitorData<FPayload>& Instance) override
		{
			return VisitOverlap(Instance);
		}

		virtual bool Raycast(const TSpatialVisitorData<FPayload>& Instance, FQueryFastData& CurData) override
		{
			return VisitRaycast(Instance, CurData);
		}

		virtual bool Sweep(const TSpatialVisitorData<FPayload>& Instance, FQueryFastData& CurData) override
		{
			return VisitSweep(Instance, CurData);
		}
	};


	auto BuildBoxes(TUniquePtr<TBox<FReal,3>>& Box, FReal BoxSize = 100, const FVec3& BoxGridDimensions = FVec3(10,10,10))
	{
		Box = MakeUnique<TBox<FReal, 3>>(FVec3(0, 0, 0), FVec3(BoxSize, BoxSize, BoxSize));
		auto Boxes = MakeUnique<FGeometryParticles>();
		const int32 NumRows = BoxGridDimensions.X;
		const int32 NumCols = BoxGridDimensions.Y;
		const int32 NumHeight = BoxGridDimensions.Z;

		Boxes->AddParticles(NumRows * NumCols * NumHeight);

		int32 Idx = 0;
		for (int32 Height = 0; Height < NumHeight; ++Height)
		{
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				for (int32 Col = 0; Col < NumCols; ++Col)
				{
					Boxes->SetGeometry(Idx, MakeSerializable(Box));
					Boxes->X(Idx) = FVec3(Col * 100, Row * 100, Height * 100);
					Boxes->R(Idx) = FRotation3::Identity;
					Boxes->LocalBounds(Idx) = Box->BoundingBox();
					Boxes->HasBounds(Idx) = true;
					Boxes->SetWorldSpaceInflatedBounds(Idx, Box->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(Idx), Boxes->R(Idx))));
					++Idx;
				}
			}
		}

		return Boxes;
	}
	
	template <typename TSpatial>
	void SpatialTestHelper(TSpatial& Spatial, FGeometryParticles* Boxes, TUniquePtr<TBox<FReal,3>>& Box, FSpatialAccelerationIdx SpatialIdx = FSpatialAccelerationIdx())
	{
		//raycast
		//miss
		{
			FVisitor Visitor(FVec3(-100, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//gather along ray
		{
			FVisitor Visitor(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//gather along ray and then make modifications
		{
			auto Spatial2 = Spatial.Copy();
			FVisitor Visitor(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Spatial2->Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);

			//remove from structure
			Spatial2->RemoveElementFrom(Visitor.Instances[0], SpatialIdx);

			FVisitor Visitor2(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Spatial2->Raycast(Visitor2.Start, Visitor2.Dir, 1000, Visitor2);
			EXPECT_EQ(Visitor2.Instances.Num(), 9);

			//move instance away
			{
				const int32 MoveIdx = Visitor2.Instances[0];
				Boxes->X(MoveIdx) += FVec3(1000, 0, 0);
				FAABB3 NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				FVisitor Visitor3(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 8);

				//move instance back
				Boxes->X(MoveIdx) -= FVec3(1000, 0, 0);
				NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);
			}

			//move other instance into view
			{
				const int32 MoveIdx = 5 * 5 * 5;
				const FVec3 OldPos = Boxes->X(MoveIdx);
				Boxes->X(MoveIdx) = FVec3(0, 0, 0);
				FAABB3 NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				FVisitor Visitor3(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 10);

				//move instance back
				Boxes->X(MoveIdx) = OldPos;
				NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);
			}

			//move instance outside of grid bounds
			{
				const int32 MoveIdx = 5 * 5 * 5;
				const FVec3 OldPos = Boxes->X(MoveIdx);
				Boxes->X(MoveIdx) = FVec3(-50, 0, 0);
				FAABB3 NewBounds = Boxes->Geometry(MoveIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(MoveIdx), Boxes->R(MoveIdx)));
				Spatial2->UpdateElementIn(MoveIdx, NewBounds, true, SpatialIdx);

				FVisitor Visitor3(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor3.Start, Visitor3.Dir, 1000, Visitor3);
				EXPECT_EQ(Visitor3.Instances.Num(), 10);

				//try ray outside of bounds which should hit
				FVisitor Visitor4(FVec3(-20, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor4.Start, Visitor4.Dir, 1000, Visitor4);
				EXPECT_EQ(Visitor4.Instances.Num(), 1);

				//delete dirty instance
				Spatial2->RemoveElementFrom(MoveIdx, SpatialIdx);
				FVisitor Visitor5(FVec3(-20, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor5.Start, Visitor5.Dir, 1000, Visitor4);
				EXPECT_EQ(Visitor5.Instances.Num(), 0);

				//move instance back
				Boxes->X(MoveIdx) = OldPos;

				//create a new box
				const int32 NewIdx = Boxes->Size();
				Boxes->AddParticles(1);
				Boxes->SetGeometry(NewIdx, MakeSerializable(Box));
				Boxes->X(NewIdx) = FVec3(-20, 0, 0);
				Boxes->R(NewIdx) = FRotation3::Identity;
				NewBounds = Boxes->Geometry(NewIdx)->template GetObject<TBox<FReal, 3>>()->BoundingBox().TransformedAABB(FRigidTransform3(Boxes->X(NewIdx), Boxes->R(NewIdx)));
				Spatial2->UpdateElementIn(NewIdx, NewBounds, true, SpatialIdx);
				FVisitor Visitor6(FVec3(-20, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
				Spatial2->Raycast(Visitor6.Start, Visitor6.Dir, 1000, Visitor6);
				EXPECT_EQ(Visitor6.Instances.Num(), 1);
			}
		}

		//stop half way through
		{
			FVisitor Visitor(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 499, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 5);
		}

		//any
		{
			FVisitor Visitor(FVec3(10, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Visitor.bAny = true;
			Spatial.Raycast(Visitor.Start, Visitor.Dir, 1000, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 1);
		}

		//sweep
		//miss
		{
			FVisitor Visitor(FVec3(-100, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = FVec3(10, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 1000, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//gather along ray
		{
			FVisitor Visitor(FVec3(-100, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = FVec3(110, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 1000, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//stop half way through
		{
			FVisitor Visitor(FVec3(-100, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = FVec3(110, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 499, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 5);
		}

		//right on edge and corner
		{
			FVisitor Visitor(FVec3(100, 0, 0), FVec3(0, 1, 0), 0, *Boxes);
			Visitor.HalfExtents = FVec3(10, 0, 0);
			Spatial.Sweep(Visitor.Start, Visitor.Dir, 499, Visitor.HalfExtents, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 10);
		}

		//overlap
		//miss
		{
			FOverlapVisitor Visitor(FAABB3(FVec3(-100, 0, 0), FVec3(-10, 0, 0)), *Boxes);
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 0);
		}

		//overlap some
		{
			FOverlapVisitor Visitor(FAABB3(FVec3(-100, 0, -10), FVec3(110, 110, 10)), *Boxes);
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 4);
		}

		//overlap any
		{
			FOverlapVisitor Visitor(FAABB3(FVec3(-100, 0, -10), FVec3(110, 110, 10)), *Boxes);
			Visitor.bAny = true;
			Spatial.Overlap(Visitor.Bounds, Visitor);
			EXPECT_EQ(Visitor.Instances.Num(), 1);
		}
	}

	void GridBPTest()
	{
		TUniquePtr<TBox<FReal, 3>> Box;
		auto Boxes = BuildBoxes(Box);
		TBoundingVolume<int32> Spatial(MakeParticleView(Boxes.Get()));
		SpatialTestHelper(Spatial, Boxes.Get(), Box);
	}

	void GridBPTest2()
	{
		TUniquePtr<TBox<FReal, 3>> Box = MakeUnique<TBox<FReal, 3>>(FVec3(0, 0, 0), FVec3(100, 100, 100));
		FPBDRigidsSOAs SOAs;
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
					Boxes.X(Idx) = FVec3(Col * 100, Row * 100, Height * 100);
					Boxes.R(Idx) = FRotation3::Identity;
					Boxes.LocalBounds(Idx) = Box->BoundingBox();
					Boxes.HasBounds(Idx) = true;
					Boxes.SetWorldSpaceInflatedBounds(Idx, Box->BoundingBox().TransformedAABB(FRigidTransform3(Boxes.X(Idx), Boxes.R(Idx))));
					++Idx;
				}
			}
		}

		TArray<TSOAView<FGeometryParticles>> TmpArray = { &Boxes };
		TBoundingVolume<FGeometryParticleHandle*> BV(MakeParticleView(MoveTemp(TmpArray)));
		TArray<FGeometryParticleHandle*> Handles = BV.FindAllIntersections(FAABB3(FVec3(0), FVec3(10)));
		EXPECT_EQ(Handles.Num(), 1);
		EXPECT_EQ(Handles[0], Boxes.Handle(0));

		Handles = BV.FindAllIntersections(FAABB3(FVec3(0), FVec3(0, 0, 110)));
		EXPECT_EQ(Handles.Num(), 2);

		//create BV with an array of handles instead (useful for partial structures)
		{
			TBoundingVolume<FGeometryParticleHandle*> BV2(MakeHandleView(Handles));
			TArray<FGeometryParticleHandle*> Handles2 = BV2.FindAllIntersections(FAABB3(FVec3(0), FVec3(10)));
			EXPECT_EQ(Handles2.Num(), 1);
			EXPECT_EQ(Handles2[0], Boxes.Handle(0));

			Handles2 = BV2.FindAllIntersections(FAABB3(FVec3(0), FVec3(0, 0, 110)));
			EXPECT_EQ(Handles2.Num(), 2);
		}
	}

	void AABBTreeTest()
	{
		using TreeType = TAABBTree<int32, TBoundingVolume<int32>>;
		{
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes = BuildBoxes(Box);
			TreeType Spatial(MakeParticleView(Boxes.Get()));

			while (!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			SpatialTestHelper(Spatial, Boxes.Get(), Box);
		}

		{
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes = BuildBoxes(Box);
			TreeType Spatial(MakeParticleView(Boxes.Get()));

			while (!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			SpatialTestHelper(Spatial, Boxes.Get(), Box);
		}

		{
			//too many boxes so reoptimize
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes = BuildBoxes(Box);
			TreeType Spatial(MakeParticleView(Boxes.Get()));

			while(!Spatial.IsAsyncTimeSlicingComplete())
			{
				Spatial.ProgressAsyncTimeSlicing(false);
			}

			EXPECT_EQ(Spatial.NumDirtyElements(),0);

			//fill up until dirty limit
			int32 Count;
			for(Count = 1; Count <= 10; ++Count)
			{
				auto Boxes2 = BuildBoxes(Box);
				for(uint32 Idx = 0; Idx < Boxes2->Size(); ++Idx)
				{
					Spatial.UpdateElement(Idx + Boxes->Size() * Count,Boxes2->WorldSpaceInflatedBounds(Idx),true);
				}

				EXPECT_EQ(Spatial.NumDirtyElements(), (Count)*Boxes->Size());
			}

			//finally pass dirty limit so reset to 0 and then add the remaining new boxes
			auto Boxes2 = BuildBoxes(Box);
			for(uint32 Idx = 0; Idx < Boxes2->Size(); ++Idx)
			{
				Spatial.UpdateElement(Idx + Boxes->Size() * (Count),Boxes2->WorldSpaceInflatedBounds(Idx),true);
			}

			EXPECT_EQ(Spatial.NumDirtyElements(),Boxes->Size() - 1);
		}
	}
	

	void AABBTreeTimesliceTest()
	{
		using TreeType = TAABBTree<int32, TAABBTreeLeafArray<int32>>;

		TUniquePtr<TBox<FReal, 3>> Box;
		auto Boxes = BuildBoxes(Box);

		// build AABB in one go
		TreeType SpatialBuildImmediate(
			MakeParticleView(Boxes.Get()) 
			, TreeType::DefaultMaxChildrenInLeaf
			, TreeType::DefaultMaxTreeDepth
			, TreeType::DefaultMaxPayloadBounds
			, 0); // build entire tree in one go, no timeslicing

		EXPECT_TRUE(SpatialBuildImmediate.IsAsyncTimeSlicingComplete());
		
		// build AABB in time-sliced sections
		TreeType SpatialTimesliced(
			MakeParticleView(Boxes.Get())
			, TreeType::DefaultMaxChildrenInLeaf
			, TreeType::DefaultMaxTreeDepth
			, TreeType::DefaultMaxPayloadBounds
			, 20); // build in small iteration steps, 20 iterations per call to ProgressAsyncTimeSlicing

		EXPECT_FALSE(SpatialTimesliced.IsAsyncTimeSlicingComplete());

		while (!SpatialTimesliced.IsAsyncTimeSlicingComplete())
		{
			SpatialTimesliced.ProgressAsyncTimeSlicing(false);
		}	

		// now check both AABBs have the same hierarchy
		// (indices will be different but walking tree should give same results)

		FAABB3 Tmp = FAABB3::ZeroAABB();

		TArray<FAABB3> AllBoundsBuildImmediate;
		SpatialBuildImmediate.GetAsBoundsArray(AllBoundsBuildImmediate, 0, -1, Tmp);

		TArray<FAABB3> AllBoundsTimesliced;
		SpatialTimesliced.GetAsBoundsArray(AllBoundsTimesliced, 0, -1, Tmp);

		EXPECT_EQ(AllBoundsBuildImmediate.Num(), AllBoundsTimesliced.Num());

		for (int i=0; i<AllBoundsBuildImmediate.Num(); i++)
		{
			EXPECT_EQ(AllBoundsBuildImmediate[i].Center(), AllBoundsTimesliced[i].Center());
			EXPECT_EQ(AllBoundsBuildImmediate[i].Extents(), AllBoundsTimesliced[i].Extents());
		}
	}

	void BroadphaseCollectionTest()
	{
		using TreeType = TAABBTree<int32, TAABBTreeLeafArray<int32>>;
		{
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes = BuildBoxes(Box);
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
			using BVType = TBoundingVolume<int32>;
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes0 = BuildBoxes(Box);
			auto Spatial0 = MakeUnique<TreeType>(MakeParticleView(Boxes0.Get()));
			while (!Spatial0->IsAsyncTimeSlicingComplete())
			{
				Spatial0->ProgressAsyncTimeSlicing(false);
			}

			FGeometryParticles EmptyBoxes;
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
			using BVType = TBoundingVolume<int32>;
			TUniquePtr<TBox<FReal, 3>> Box;
			auto Boxes1 = BuildBoxes(Box);
			FGeometryParticles EmptyBoxes;

			auto Spatial0 = MakeUnique<TreeType>(MakeParticleView(&EmptyBoxes));
			auto Spatial1 = MakeUnique<BVType>(MakeParticleView(Boxes1.Get()));

			TSpatialAccelerationCollection<TreeType, BVType> AccelerationCollection;
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial0), 0);
			AccelerationCollection.AddSubstructure(MoveTemp(Spatial1), 1);

			FSpatialAccelerationIdx SpatialIdx = { 1,0 };
			SpatialTestHelper(AccelerationCollection, Boxes1.Get(), Box, SpatialIdx);
		}
	}


	void SpatialAccelerationDirtyAndGlobalQueryStrestTest()
	{
		using AABBTreeType = TAABBTree<FAccelerationStructureHandle, TAABBTreeLeafArray<FAccelerationStructureHandle>>;

		// Construct 100000 Particles
		const int32 NumRows = 100;
		const int32 NumCols = 100;
		const int32 NumHeight = 10;
		const int32 ParticleCount = NumRows * NumCols * NumHeight;
		const FReal BoxSize = 100;

		FPBDRigidsSOAs Particles;
		TArray<FPBDRigidParticleHandle*> ParticleHandles = Particles.CreateDynamicParticles(ParticleCount);
		for (auto& Handle : ParticleHandles)
		{
			Handle->GTGeometryParticle() = FGeometryParticle::CreateParticle().Release();
		}
		const auto& ParticlesView = Particles.GetAllParticlesView();

		// ensure these can't be filtered out.
		FCollisionFilterData FilterData;
		FilterData.Word0 = TNumericLimits<uint32>::Max();
		FilterData.Word1 = TNumericLimits<uint32>::Max();
		FilterData.Word2 = TNumericLimits<uint32>::Max();
		FilterData.Word3 = TNumericLimits<uint32>::Max();

		TSharedPtr<TBox<FReal, 3>, ESPMode::ThreadSafe> Box = MakeShared<TBox<FReal, 3>, ESPMode::ThreadSafe>(FVec3(0, 0, 0), FVec3(BoxSize, BoxSize, BoxSize));

		int32 Idx = 0;
		for (int32 Height = 0; Height < NumHeight; ++Height)
		{
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				for (int32 Col = 0; Col < NumCols; ++Col)
				{
					FGeometryParticle* GTParticle = ParticleHandles[Idx]->GTGeometryParticle();
					FPBDRigidParticleHandle* Handle = ParticleHandles[Idx];

					Handle->SetGeometry(MakeSerializable(Box));
					Handle->ShapesArray()[0]->SetQueryData(FilterData);
					GTParticle->SetGeometry(Box);
					GTParticle->ShapesArray()[0]->SetQueryData(FilterData);
					Handle->SetX(FVec3(Col * BoxSize, Row * BoxSize, Height * BoxSize));
					GTParticle->SetX(FVec3(Col * BoxSize, Row * BoxSize, Height * BoxSize));
					Handle->SetR(FRotation3::Identity);
					GTParticle->SetR(FRotation3::Identity);
					Handle->SetUniqueIdx(FUniqueIdx(Idx));
					GTParticle->SetUniqueIdx(FUniqueIdx(Idx));
					Handle->SetLocalBounds(Box->BoundingBox());
					Handle->SetHasBounds(true);
					Handle->SetWorldSpaceInflatedBounds(Box->BoundingBox().TransformedAABB(FRigidTransform3(GTParticle->X(), GTParticle->R())));
					++Idx;
				}
			}
		}

		int32 DirtyNum = 800;
		int32 Queries = 500;
		ensure(DirtyNum < ParticleCount);

		// Construct tree
		AABBTreeType Spatial(ParticlesView);

		// Update DirtyNum elements, so they are pulled out of leaves.
		for (int32 i = 0; i < DirtyNum; ++i)
		{
			FAccelerationStructureHandle Payload(ParticleHandles[i]->GTGeometryParticle());
			FAABB3 Bounds = ParticleHandles[i]->WorldSpaceInflatedBounds();
			Spatial.UpdateElement(Payload, Bounds, true);
		}

		// RAYCASTS
		{
			// Setup raycast params
			const FVec3 Start(500, 500, 500);
			const FVec3 Dir(1, 0, 0);
			const FReal Length = 1000;
			FStressTestVisitor Visitor;

			// Measure raycasts
			uint32 Cycles = 0.0;
			for (int32 Query = 0; Query < Queries; ++Query)
			{
				uint32 StartTime = FPlatformTime::Cycles();

				Spatial.Raycast(Start, Dir, Length, Visitor);

				Cycles += FPlatformTime::Cycles() - StartTime;
			}

			float Milliseconds = FPlatformTime::ToMilliseconds(Cycles);
			float AvgMicroseconds = (Milliseconds * 1000) / Queries;

			UE_LOG(LogHeadlessChaos, Warning, TEXT("Raycast Test: Dirty Particles: %d, Queries: %d, Avg Query Time: %f(us), Total:%f(ms)"), DirtyNum, Queries, AvgMicroseconds, Milliseconds);
		}

		// SWEEPS
		{
			// Setup Sweep params
			const FVec3 Start(500, 500, 500);
			const FVec3 Dir(1, 0, 0);
			const FReal Length = 1000;
			const FVec3 HalfExtents(50, 50, 50);
			FStressTestVisitor Visitor;

			// Measure raycasts
			uint32 Cycles = 0.0;
			for (int32 Query = 0; Query < Queries; ++Query)
			{
				uint32 StartTime = FPlatformTime::Cycles();

				Spatial.Sweep(Start, Dir, Length, HalfExtents, Visitor);

				Cycles += FPlatformTime::Cycles() - StartTime;
			}

			float Milliseconds = FPlatformTime::ToMilliseconds(Cycles);
			float AvgMicroseconds = (Milliseconds * 1000) / Queries;

			UE_LOG(LogHeadlessChaos, Warning, TEXT("Sweep Test: Dirty Particles: %d, Queries: %d, Avg Query Time: %f(us), Total:%f(ms)"), DirtyNum, Queries, AvgMicroseconds, Milliseconds);
		}

		// OVERLAPS
		{
			FStressTestVisitor Visitor;
			const FAABB3 QueryBounds(FVec3(-50, -50, -50), FVec3(50,50,50));

			// Measure raycasts
			uint32 Cycles = 0.0;
			for (int32 Query = 0; Query < Queries; ++Query)
			{
				uint32 StartTime = FPlatformTime::Cycles();

				Spatial.Overlap(QueryBounds, Visitor);

				Cycles += FPlatformTime::Cycles() - StartTime;
			}

			float Milliseconds = FPlatformTime::ToMilliseconds(Cycles);
			float AvgMicroseconds = (Milliseconds * 1000) / Queries;

			UE_LOG(LogHeadlessChaos, Error, TEXT("Overlap Test: Dirty Particles: %d, Queries: %d, Avg Query Time: %f(us), Total:%f(ms)"), DirtyNum, Queries, AvgMicroseconds, Milliseconds);
		}
	}

}