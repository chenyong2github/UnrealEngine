// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ArrayND.h"
#include "Chaos/BoundingVolumeUtilities.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/Transform.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/ISpatialAcceleration.h"
#include "ChaosStats.h"
#include "HAL/IConsoleManager.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Templates/Models.h"

#include <memory>
#include <unordered_set>

// Required for debug blocks below in raycasts
//#include "Engine/Engine.h"
//#include "Engine/World.h"
//#include "DrawDebugHelpers.h"

template <typename T, bool>
struct TSpatialAccelerationTraits
{
};

template <typename TSOA>
struct TSpatialAccelerationTraits<TSOA, true>
{
	using TPayloadType = typename TSOA::THandleType;
};

template <typename T>
struct TSpatialAccelerationTraits<T, false>
{
	using TPayloadType = int32;
};

struct CParticleView
{
	template <typename T>
	auto Requires(typename T::THandleType) ->void;
};

struct FBoundingVolumeCVars
{
	static int32 FilterFarBodies;
	static FAutoConsoleVariableRef CVarFilterFarBodies;
};

DECLARE_CYCLE_STAT(TEXT("BoundingVolumeGenerateTree"), STAT_BoundingVolumeGenerateTree, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("BoundingVolumeComputeGlobalBox"), STAT_BoundingVolumeComputeGlobalBox, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("BoundingVolumeFillGrid"), STAT_BoundingVolumeFillGrid, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("BoundingVolumeRemoveElement"), STAT_BoundingVolumeRemoveElement, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("BoundingVolumeUpdateElement"), STAT_BoundingVolumeUpdateElement, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("BoundingVolumeAddElement"), STAT_BoundingVolumeAddElement, STATGROUP_Chaos);

namespace Chaos
{
template<typename TPayloadType, typename T, int d>
struct TBVCellElement
{
	TBox<T, d> Bounds;
	TPayloadType Payload;
	TVector<int32, 3> StartIdx;
	TVector<int32, 3> EndIdx;

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Bounds;
		Ar << Payload;
		Ar << StartIdx;
		Ar << EndIdx;
	}
};

template<typename TPayloadType, class T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TBVCellElement<TPayloadType, T, d>& Elem)
{
	Elem.Serialize(Ar);
	return Ar;
}

template <typename T, int d>
struct TBVPayloadInfo
{
	int32 GlobalPayloadIdx;
	int32 DirtyPayloadIdx;
	TVector<int32, d> StartIdx;
	TVector<int32, d> EndIdx;

	void Serialize(FArchive& Ar)
	{
		Ar << GlobalPayloadIdx;
		Ar << DirtyPayloadIdx;
		Ar << StartIdx;
		Ar << EndIdx;
	}
};

template <typename T, int d>
FArchive& operator<<(FArchive& Ar, TBVPayloadInfo<T, d>& Info)
{
	Info.Serialize(Ar);
	return Ar;
}

template<typename InPayloadType, typename T, int d>
class TBoundingVolume final : public ISpatialAcceleration<InPayloadType, T,d>
{
  public:
	using TPayloadType = InPayloadType;
	using PayloadType = TPayloadType;
	using TType = T;
	static constexpr int D = d;

	static constexpr int32 DefaultMaxCells = 15;
	static constexpr T DefaultMaxPayloadBounds = 100000;
	static constexpr ESpatialAcceleration StaticType = ESpatialAcceleration::BoundingVolume;
	TBoundingVolume()
		: ISpatialAcceleration<InPayloadType, T, d>(StaticType)
	{
	}

	template <typename ParticleView>
	TBoundingVolume(const ParticleView& Particles, const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells, const T InMaxPayloadBounds = DefaultMaxPayloadBounds)
		: ISpatialAcceleration<InPayloadType, T, d>(StaticType)
		, MaxPayloadBounds(InMaxPayloadBounds)
	{
		Reinitialize(Particles, bUseVelocity, Dt, MaxCells);
	}

	TBoundingVolume(TBoundingVolume<TPayloadType, T, d>&& Other)
		: ISpatialAcceleration<InPayloadType, T, d>(StaticType)
		, MGlobalPayloads(MoveTemp(Other.MGlobalPayloads))
		, MGrid(MoveTemp(Other.MGrid))
		, MElements(MoveTemp(Other.MElements))
		, MDirtyElements(MoveTemp(Other.MDirtyElements))
		, MPayloadInfo(MoveTemp(Other.MPayloadInfo))
		, MaxPayloadBounds(Other.MaxPayloadBounds)
		, bIsEmpty(Other.bIsEmpty)
	{
	}

	//needed for tree of grids, should we have a more explicit way to copy an array of BVs to avoid this being public?
	TBoundingVolume(const TBoundingVolume<TPayloadType, T, d>& Other)
		: ISpatialAcceleration<InPayloadType, T, d>(StaticType)
		, MGlobalPayloads(Other.MGlobalPayloads)
		, MGrid(Other.MGrid)
		, MElements(Other.MElements.Copy())
		, MDirtyElements(Other.MDirtyElements)
		, MPayloadInfo(Other.MPayloadInfo)
		, MaxPayloadBounds(Other.MaxPayloadBounds)
		, bIsEmpty(Other.bIsEmpty)
	{
	}

public:
	TBoundingVolume<TPayloadType, T, d>& operator=(const TBoundingVolume<TPayloadType, T, d>& Other) = delete;
	TBoundingVolume<TPayloadType, T, d>& operator=(TBoundingVolume<TPayloadType, T, d>&& Other)
	{
		MGlobalPayloads = MoveTemp(Other.MGlobalPayloads);
		MGrid = MoveTemp(Other.MGrid);
		MElements = MoveTemp(Other.MElements);
		MDirtyElements = MoveTemp(Other.MDirtyElements);
		MPayloadInfo = MoveTemp(Other.MPayloadInfo);
		MaxPayloadBounds = Other.MaxPayloadBounds;
		bIsEmpty = Other.bIsEmpty;
		return *this;
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>(new TBoundingVolume<TPayloadType, T, d>(*this));
	}

	template <typename ParticleView>
	void Reinitialize(const ParticleView& Particles, const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells)
	{
		GenerateTree(Particles, bUseVelocity, Dt, MaxCells);
	}

	TArray<TPayloadType> FindAllIntersectionsImp(const TBox<T,d>& Intersection) const
	{
		struct FSimpleVisitor
		{
			FSimpleVisitor(TArray<TPayloadType>& InResults) : CollectedResults(InResults) {}
			bool VisitOverlap(const TSpatialVisitorData<TPayloadType>& Instance)
			{
				CollectedResults.Add(Instance.Payload);
				return true;
			}
			TArray<TPayloadType>& CollectedResults;
		};

		TArray<TPayloadType> Results;
		FSimpleVisitor Collector(Results);
		Overlap(Intersection, Collector);

		return Results;
	}

	virtual void RemoveElement(const TPayloadType& Payload) override
	{
		SCOPE_CYCLE_COUNTER(STAT_BoundingVolumeRemoveElement);
		if (const FPayloadInfo* PayloadInfo = MPayloadInfo.Find(Payload))
		{
			if (PayloadInfo->GlobalPayloadIdx != INDEX_NONE)
			{
				ensure(PayloadInfo->DirtyPayloadIdx == INDEX_NONE);
				auto LastGlobalPayload = MGlobalPayloads.Last().Payload;
				if (!(LastGlobalPayload == Payload))
				{
					MPayloadInfo.FindChecked(LastGlobalPayload).GlobalPayloadIdx = PayloadInfo->GlobalPayloadIdx;
				}
				MGlobalPayloads.RemoveAtSwap(PayloadInfo->GlobalPayloadIdx);
			}
			else
			{
				RemoveElementFromExistingGrid(Payload, *PayloadInfo);
			}

			MPayloadInfo.Remove(Payload);
		}
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TBox<T,d>& NewBounds, bool bHasBounds) override
	{
		SCOPE_CYCLE_COUNTER(STAT_BoundingVolumeUpdateElement);
		if (FPayloadInfo* PayloadInfo = MPayloadInfo.Find(Payload))
		{
			ensure(bHasBounds || PayloadInfo->GlobalPayloadIdx != INDEX_NONE);
			if (PayloadInfo->GlobalPayloadIdx == INDEX_NONE)
			{
				RemoveElementFromExistingGrid(Payload, *PayloadInfo);
				AddElementToExistingGrid(Payload, *PayloadInfo, NewBounds, bHasBounds);
			}
		}
		else
		{
			FPayloadInfo& NewPayloadInfo = MPayloadInfo.Add(Payload);
			AddElementToExistingGrid(Payload, NewPayloadInfo, NewBounds, bHasBounds);
		}
	}

	// Begin ISpatialAcceleration interface
	virtual TArray<TPayloadType> FindAllIntersections(const TBox<T, d>& Box) const override { return FindAllIntersectionsImp(Box); }

	const TArray<TPayloadBoundsElement<TPayloadType, T>>& GlobalObjects() const
	{
		return MGlobalPayloads;
	}

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Raycast(Start, Dir, OriginalLength, ProxyVisitor);
	}

	template <typename SQVisitor>
	bool RaycastFast(const TVector<T,d>& Start, const TVector<T,d>& Dir, const TVector<T,d>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, SQVisitor& Visitor) const
	{
		if (Raycast(Start, Dir, CurrentLength, Visitor))
		{
			InvCurrentLength = 1 / CurrentLength;
			return true;
		}
		return false;
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	bool Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, SQVisitor& Visitor) const
	{
		T CurrentLength = OriginalLength;

		bool bParallel[d];
		TVector<T, d> InvDir;

		T InvCurrentLength = 1 / CurrentLength;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		TVector<T, d> TmpPosition;
		T TOI;

		for (const auto& Elem : MGlobalPayloads)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TBox<T, d>::RaycastFast(InstanceBounds.Min(), InstanceBounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = Visitor.VisitRaycast(VisitData, CurrentLength);
				if (!bContinue)
				{
					return false;
				}
				InvCurrentLength = 1 / CurrentLength;
			}
		}

		for (const auto& Elem : MDirtyElements)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TBox<T, d>::RaycastFast(InstanceBounds.Min(), InstanceBounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = Visitor.VisitRaycast(VisitData, CurrentLength);
				if (!bContinue)
				{
					return false;
				}
				InvCurrentLength = 1 / CurrentLength;
			}
		}

		TBox<T, d> GlobalBounds(MGrid.MinCorner(), MGrid.MaxCorner());
		TVector<T, d> NextStart;
		TVector<int32, d> CellIdx;
		bool bCellsLeft = MElements.Num() && TBox<T,d>::RaycastFast(GlobalBounds.Min(), GlobalBounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, NextStart);
		if (bCellsLeft)
		{
			CellIdx = MGrid.Cell(NextStart);
			CellIdx = MGrid.ClampIndex(CellIdx);	//raycast may have ended slightly outside of grid
			FGridSet CellsVisited(MGrid.Counts());

			do
			{
				//gather all instances in current cell whose bounds intersect with ray
				const auto& Elems = MElements(CellIdx);
				//should we let callback know about max potential?

				for (const auto& Elem : Elems)
				{
					if (bPruneDuplicates)
					{

						bool bSkip = false;
						if (Elem.StartIdx[0] != Elem.EndIdx[0] || Elem.StartIdx[1] != Elem.EndIdx[1] || Elem.StartIdx[2] != Elem.EndIdx[2])
						{
							for (int32 X = Elem.StartIdx[0]; X <= Elem.EndIdx[0]; ++X)
							{
								for (int32 Y = Elem.StartIdx[1]; Y <= Elem.EndIdx[1]; ++Y)
								{
									for (int32 Z = Elem.StartIdx[2]; Z <= Elem.EndIdx[2]; ++Z)
									{
										if (CellsVisited.Contains(TVector<int32, 3>(X, Y, Z)))
										{
											bSkip = true;
											break;
										}
									}
								}
							}

							if (bSkip)
							{
								continue;
							}
						}
					}
					const auto& InstanceBounds = Elem.Bounds;
					if (TBox<T,d>::RaycastFast(InstanceBounds.Min(), InstanceBounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
					{
						TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
						const bool bContinue = Visitor.VisitRaycast(VisitData, CurrentLength);
						if (!bContinue)
						{
							return false;
						}
						InvCurrentLength = 1 / CurrentLength;
					}
				}

				CellsVisited.Add(CellIdx);


				//find next cell

				//We want to know which plane we used to cross into next cell
				const TVector<T, d> CellCenter = MGrid.Location(CellIdx);
				const TVector<T, d>& Dx = MGrid.Dx();

				T Times[3];
				T BestTime = CurrentLength;
				bool bTerminate = true;
				for (int Axis = 0; Axis < d; ++Axis)
				{
					if (!bParallel[Axis])
					{
						const T CrossPoint = Dir[Axis] > 0 ? CellCenter[Axis] + Dx[Axis] / 2 : CellCenter[Axis] - Dx[Axis] / 2;
						const T Distance = CrossPoint - NextStart[Axis];	//note: CellCenter already has /2, we probably want to use the corner instead
						const T Time = Distance * InvDir[Axis];
						Times[Axis] = Time;
						if (Time < BestTime)
						{
							bTerminate = false;	//found at least one plane to pass through
							BestTime = Time;
						}
					}
					else
					{
						Times[Axis] = TNumericLimits<T>::Max();
					}
				}

				if (bTerminate)
				{
					return true;
				}

				for (int Axis = 0; Axis < d; ++Axis)
				{
					constexpr T Epsilon = 1e-2;	//if raycast is slightly off we still count it as hitting the cell surface
					CellIdx[Axis] += (Times[Axis] <= BestTime + Epsilon) ? (Dir[Axis] > 0 ? 1 : -1) : 0;
					if (CellIdx[Axis] < 0 || CellIdx[Axis] >= MGrid.Counts()[Axis])
					{
						return true;
					}
				}

				NextStart = NextStart + Dir * BestTime;
			} while (true);
		}
		
		return true;
	}

	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Sweep(Start, Dir, OriginalLength, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	bool SweepFast(const TVector<T,d>& Start, const TVector<T,d>& Dir, const TVector<T,d>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, const TVector<T,d>& QueryHalfExtents, SQVisitor& Visitor) const
	{
		if (Sweep(Start, Dir, CurrentLength, QueryHalfExtents, Visitor))
		{
			InvCurrentLength = 1 / CurrentLength;
			return true;
		}
		return false;
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	bool Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, SQVisitor& Visitor) const
	{
		T CurrentLength = OriginalLength;

		T TOI = 0;	//not needed, but fixes compiler warning
		bool bParallel[d];
		TVector<T, d> InvDir;

		T InvCurrentLength = 1 / CurrentLength;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		for (const auto& Elem : MGlobalPayloads)
		{
			const TBox<T, d>& InstanceBounds = Elem.Bounds;
			const TVector<T, d> Min = InstanceBounds.Min() - QueryHalfExtents;
			const TVector<T, d> Max = InstanceBounds.Max() + QueryHalfExtents;
			TVector<T, d> TmpPosition;
			if (TBox<T, d>::RaycastFast(Min, Max, Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = Visitor.VisitSweep(VisitData, CurrentLength);
				if (!bContinue)
				{
					return false;
				}
				InvCurrentLength = 1 / CurrentLength;
			}
		}

		for (const auto& Elem : MDirtyElements)
		{
			const TBox<T, d>& InstanceBounds = Elem.Bounds;
			const TVector<T, d> Min = InstanceBounds.Min() - QueryHalfExtents;
			const TVector<T, d> Max = InstanceBounds.Max() + QueryHalfExtents;
			TVector<T, d> TmpPosition;
			if (TBox<T, d>::RaycastFast(Min, Max, Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = Visitor.VisitSweep(VisitData, CurrentLength);
				if (!bContinue)
				{
					return false;
				}
				InvCurrentLength = 1 / CurrentLength;
			}
		}

		if (MElements.Num() == 0)
		{
			return true;
		}

		TBox<T, d> GlobalBounds(MGrid.MinCorner() - QueryHalfExtents, MGrid.MaxCorner() + QueryHalfExtents);
		

		struct FCellIntersection
		{
			TVector<int32, d> CellIdx;
			T TOI;
		};

		
		TVector<T, d> HitPoint;
		FGridSet IdxsSeen(MGrid.Counts());
		FGridSet CellsVisited(MGrid.Counts());
		const bool bInitialHit = TBox<T,d>::RaycastFast(GlobalBounds.Min(), GlobalBounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, HitPoint);
		if (bInitialHit)
		{
			//Flood fill from inflated cell so that we get all cells along the ray
			TVector<int32, d> HitCellIdx = MGrid.Cell(HitPoint);
			HitCellIdx = MGrid.ClampIndex(HitCellIdx);	//inflation means we likely are outside grid, just get closest cell

			TArray<FCellIntersection> IdxsQueue;	//cells we need to visit
			IdxsQueue.Add({ HitCellIdx, TOI });

			const TVector<T, d> HalfDx = MGrid.Dx() * (T)0.5;

			int32 QueueIdx = 0;	//FIFO because early cells are more likely to block later cells we can skip
			while (QueueIdx < IdxsQueue.Num())
			{
				const FCellIntersection CellIntersection = IdxsQueue[QueueIdx++];
				if (CellIntersection.TOI > CurrentLength)
				{
					continue;
				}

				//ray still visiting this cell so check all neighbors
				check(d == 3);
				static const TVector<int32, 3> Neighbors[] =
				{
					//grid on z=-1 plane
					{-1, -1, -1}, {0, -1, -1}, {1, -1, -1},
					{-1, 0, -1}, {0, 0, -1}, {1, 0, -1},
					{-1, 1, -1}, {0, 1, -1}, {1, 1, -1},

					//grid on z=0 plane
					{-1, -1, 0}, {0, -1, 0}, {1, -1, 0},
					{-1, 0, 0},			 {1, 0, 0},
					{-1, 1, 0}, {0, 1, 0}, {1, 0, 0},

					//grid on z=1 plane
					{-1, -1, 1}, {0, -1, 1}, {1, -1, 1},
					{-1, 0, 1}, {0, 0, 1}, {1, 0, 1},
					{-1, 1, 1}, {0, 1, 1}, {1, 1, 1}
				};

				for (const TVector<int32, 3>& Neighbor : Neighbors)
				{
					const TVector<int32, 3> NeighborIdx = Neighbor + CellIntersection.CellIdx;
					bool bSkip = false;
					for (int32 Axis = 0; Axis < d; ++Axis)
					{
						if (NeighborIdx[Axis] < 0 || NeighborIdx[Axis] >= MGrid.Counts()[Axis])
						{
							bSkip = true;
							break;
						}
					}
					if (!bSkip && !IdxsSeen.Contains(NeighborIdx))
					{
						IdxsSeen.Add(NeighborIdx);

						const TVector<T, d> NeighborCenter = MGrid.Location(NeighborIdx);
						const TVector<T, d> Min = NeighborCenter - QueryHalfExtents - HalfDx;
						const TVector<T, d> Max = NeighborCenter + QueryHalfExtents + HalfDx;
						if (TBox<T,d>::RaycastFast(Min, Max, Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, HitPoint))
						{
							IdxsQueue.Add({ NeighborIdx, TOI });	//should we sort by TOI?
						}
					}
				}

				//check if any instances in the cell are hit
				const auto& Elems = MElements(CellIntersection.CellIdx);
				for (const auto& Elem : Elems)
				{
					if (bPruneDuplicates)
					{
						bool bSkip = false;
						if (Elem.StartIdx[0] != Elem.EndIdx[0] || Elem.StartIdx[1] != Elem.EndIdx[1] || Elem.StartIdx[2] != Elem.EndIdx[2])
						{
							for (int32 X = Elem.StartIdx[0]; X <= Elem.EndIdx[0]; ++X)
							{
								for (int32 Y = Elem.StartIdx[1]; Y <= Elem.EndIdx[1]; ++Y)
								{
									for (int32 Z = Elem.StartIdx[2]; Z <= Elem.EndIdx[2]; ++Z)
									{
										if (CellsVisited.Contains(TVector<int32,3>(X,Y,Z)))
										{
											bSkip = true;
											break;
										}
									}
								}
							}

							if (bSkip)
							{
								continue;
							}
						}
					}

					const TBox<T, d>& InstanceBounds = Elem.Bounds;
					const TVector<T, d> Min = InstanceBounds.Min() - QueryHalfExtents;
					const TVector<T, d> Max = InstanceBounds.Max() + QueryHalfExtents;
					if (TBox<T,d>::RaycastFast(Min, Max, Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, HitPoint))
					{
						TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
						const bool bContinue = Visitor.VisitSweep(VisitData, CurrentLength);
						if (!bContinue)
						{
							return false;
						}
						InvCurrentLength = 1 / CurrentLength;
					}
				}
				CellsVisited.Add(CellIntersection.CellIdx);
			}
		}

		return true;
	}

	void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Overlap(QueryBounds, ProxyVisitor);
	}

	template <typename SQVisitor>
	bool OverlapFast(const TBox<T, d>& QueryBounds, SQVisitor& Visitor) const
	{
		return Overlap(QueryBounds, Visitor);
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	bool Overlap(const TBox<T, d>& QueryBounds, SQVisitor& Visitor) const
	{
		for (const auto& Elem : MGlobalPayloads)
		{
			const TBox<T, d>& InstanceBounds = Elem.Bounds;
			if (QueryBounds.Intersects(InstanceBounds))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				if (Visitor.VisitOverlap(VisitData) == false)
				{
					return false;
				}
			}
		}

		for (const auto& Elem : MDirtyElements)
		{
			const TBox<T, d>& InstanceBounds = Elem.Bounds;
			if (QueryBounds.Intersects(InstanceBounds))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				if (Visitor.VisitOverlap(VisitData) == false)
				{
					return false;
				}
			}
		}

		TBox<T, d> GlobalBounds(MGrid.MinCorner(), MGrid.MaxCorner());

		const TVector<int32, d> StartIndex = MGrid.ClampIndex(MGrid.Cell(QueryBounds.Min()));
		const TVector<int32, d> EndIndex = MGrid.ClampIndex(MGrid.Cell(QueryBounds.Max()));
		TSet<TPayloadType> InstancesSeen;

		for (int32 X = StartIndex[0]; X <= EndIndex[0]; ++X)
		{
			for (int32 Y = StartIndex[1]; Y <= EndIndex[1]; ++Y)
			{
				for (int32 Z = StartIndex[2]; Z <= EndIndex[2]; ++Z)
				{
					const auto& Elems = MElements(X, Y, Z);
					for (const auto& Elem : Elems)
					{
						if (bPruneDuplicates)
						{
							if (InstancesSeen.Contains(Elem.Payload))
							{
								continue;
							}
							InstancesSeen.Add(Elem.Payload);
						}
						const TBox<T, d>& InstanceBounds = Elem.Bounds;
						if (QueryBounds.Intersects(InstanceBounds))
						{
							TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
							if (Visitor.VisitOverlap(VisitData) == false)
							{
								return false;
							}
						}
					}
				}
			}
		}

		return true;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::GlobalElementsHaveBounds)
		{
			TArray<TPayloadType> TmpPayloads;
			Ar << TmpPayloads;
			MGlobalPayloads.Reserve(TmpPayloads.Num());
			for (auto& Payload : TmpPayloads)
			{
				MGlobalPayloads.Add({ Payload, TBox<T,d>(TVector<T,d>(TNumericLimits<T>::Lowest()), TVector<T,d>(TNumericLimits<T>::Max())) });
			}
			MaxPayloadBounds = DefaultMaxPayloadBounds;
		}
		else
		{
			Ar << MGlobalPayloads;
			Ar << MaxPayloadBounds;
		}

		Ar << MGrid;
		Ar << MElements;
		Ar << MDirtyElements;
		Ar << bIsEmpty;

		TArray<TPayloadType> Payloads;
		if (!Ar.IsLoading())
		{
			MPayloadInfo.GenerateKeyArray(Payloads);
		}
		Ar << Payloads;

		for (auto Payload : Payloads)
		{
			auto& Info = MPayloadInfo.FindOrAdd(Payload);
			Ar << Info;
		}

	}

private:

	using FCellElement = TBVCellElement<TPayloadType, T, d>;
	using FPayloadInfo = TBVPayloadInfo<T, d>;

	template <typename ParticleView>
	void GenerateTree(const ParticleView& Particles, const bool bUseVelocity, const T Dt, const int32 MaxCells)
	{
		SCOPE_CYCLE_COUNTER(STAT_BoundingVolumeGenerateTree);
		TArray<TBox<T, d>> AllBounds;
		TArray<bool> HasBounds;

		AllBounds.SetNum(Particles.Num());
		HasBounds.SetNum(Particles.Num());
		int32 MaxPayloadBoundsCopy = MaxPayloadBounds;
		auto GetValidBounds = [MaxPayloadBoundsCopy, bUseVelocity, Dt](const auto& Particle, TBox<T,d>& OutBounds) -> bool
		{
			if (HasBoundingBox(Particle))
			{
				OutBounds = ComputeWorldSpaceBoundingBox(Particle, bUseVelocity, Dt);	//todo: avoid computing on the fly
				//todo: check if bounds are outside of something we deem reasonable (i.e. object really far out in space)
				if (OutBounds.Extents().Max() < MaxPayloadBoundsCopy)
				{
					return true;
				}
			}

			return false;
		};

		//compute DX and fill global payloads
		MGlobalPayloads.Reset();
		auto& GlobalPayloads = MGlobalPayloads;
		auto& PayloadInfos = MPayloadInfo;
		T NumObjectsWithBounds = 0;
		MPayloadInfo.Reset();

		auto ComputeBoxAndDx = [&Particles, &AllBounds, &HasBounds, &GlobalPayloads, &PayloadInfos, &GetValidBounds, &NumObjectsWithBounds](TBox<T,d>& OutGlobalBox, bool bFirstPass) -> T
		{
			SCOPE_CYCLE_COUNTER(STAT_BoundingVolumeComputeGlobalBox);
			OutGlobalBox = TBox<T, d>::EmptyBox();
			constexpr T InvD = (T)1 / d;
			int32 Idx = 0;
			T Dx = 0;
			NumObjectsWithBounds = 0;
			for (auto& Particle : Particles)
			{
				TBox<T,d>& Bounds = AllBounds[Idx];
				if ((bFirstPass && GetValidBounds(Particle, Bounds)) || (!bFirstPass && HasBounds[Idx]))
				{
					HasBounds[Idx] = true;
					OutGlobalBox.GrowToInclude(Bounds);
					Dx += TVector<T, d>::DotProduct(Bounds.Extents(), TVector<T, d>(1)) * InvD;;
					NumObjectsWithBounds += 1;
				}
				else if(bFirstPass)
				{
					HasBounds[Idx] = false;
					auto Payload = Particle.template GetPayload<TPayloadType>(Idx);

					const int32 GlobalPayloadIdx = GlobalPayloads.Num();
					bool bTooBig = HasBoundingBox(Particle);	//todo: avoid this as it was already called in GetValidBounds
					GlobalPayloads.Add({ Payload, bTooBig ? Bounds : TBox<T,d>(TVector<T,d>(TNumericLimits<T>::Lowest()), TVector<T,d>(TNumericLimits<T>::Max())) });
					PayloadInfos.Add(Payload, FPayloadInfo{ GlobalPayloadIdx, INDEX_NONE });
				}
				++Idx;
			}

			Dx /= NumObjectsWithBounds;
			return Dx;
		};

		TBox<T, d> GlobalBox;
		T Dx = ComputeBoxAndDx(GlobalBox, /*bFirstPass=*/true);

		if (FBoundingVolumeCVars::FilterFarBodies)
		{
			bool bRecomputeBoxAndDx = false;
			int32 Idx = 0;
			for (auto& Particle : Particles)
			{
				if (HasBounds[Idx])
				{
					bool bEvictElement = false;
					const auto& WorldSpaceBox = AllBounds[Idx];
					const TVector<T, d> MinToDXRatio = WorldSpaceBox.Min() / Dx;
					for (int32 Axis = 0; Axis < d; ++Axis)
					{
						if (FMath::Abs(MinToDXRatio[Axis]) > 1e7)
						{
							bEvictElement = true;
							break;
						}
					}

					if (bEvictElement)
					{
						bRecomputeBoxAndDx = true;
						HasBounds[Idx] = false;
						auto Payload = Particle.template GetPayload<TPayloadType>(Idx);

						const int32 GlobalPayloadIdx = GlobalPayloads.Num();
						GlobalPayloads.Add({ Payload, WorldSpaceBox });
						MPayloadInfo.Add(Payload, FPayloadInfo{ GlobalPayloadIdx, INDEX_NONE});
					}
				}

				++Idx;
			}

			if (bRecomputeBoxAndDx)
			{
				Dx = ComputeBoxAndDx(GlobalBox, /*bFirstPass=*/false);
			}
		}

		TVector<int32, d> Cells = Dx > 0 ? GlobalBox.Extents() / Dx : TVector<int32, d>(MaxCells);
		Cells += TVector<int32, d>(1);
		for (int32 Axis = 0; Axis < d; ++Axis)
		{
			if (Cells[Axis] > MaxCells)
				Cells[Axis] = MaxCells;
			if (!(ensure(Cells[Axis] >= 0)))	//seeing this because GlobalBox is huge leading to int overflow. Need to investigate why bounds get so big
			{
				Cells[Axis] = MaxCells;
			}
		}

		MGrid = TUniformGrid<T, d>(GlobalBox.Min(), GlobalBox.Max(), Cells);
		MElements = TArrayND<TArray<FCellElement>, d>(MGrid);

		//insert into grid cells
		T NumObjectsInCells = 0;
		{
			SCOPE_CYCLE_COUNTER(STAT_BoundingVolumeFillGrid);
			int32 Idx = 0;
			for (auto& Particle : Particles)
			{
				if (HasBounds[Idx])
				{
					const TBox<T, d>& ObjectBox = AllBounds[Idx];
					NumObjectsWithBounds += 1;
					const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
					const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
					
					auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
					MPayloadInfo.Add(Payload, FPayloadInfo{ INDEX_NONE, INDEX_NONE, StartIndex, EndIndex });

					for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
					{
						for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
						{
							for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
							{
								MElements(x, y, z).Add({ ObjectBox, Payload, StartIndex, EndIndex });
								NumObjectsInCells += 1;
							}
						}
					}
				}
				++Idx;
			}
		}

		bIsEmpty = NumObjectsInCells == 0;
		UE_LOG(LogChaos, Verbose, TEXT("Generated Tree with (%d, %d, %d) Nodes and %f Per Cell"), MGrid.Counts()[0], MGrid.Counts()[1], MGrid.Counts()[2], NumObjectsInCells / NumObjectsWithBounds);
	}

	void RemoveElementFromExistingGrid(const TPayloadType& Payload, const FPayloadInfo& PayloadInfo)
	{
		ensure(PayloadInfo.GlobalPayloadIdx == INDEX_NONE);
		if (PayloadInfo.DirtyPayloadIdx == INDEX_NONE)
		{
			for (int32 X = PayloadInfo.StartIdx[0]; X <= PayloadInfo.EndIdx[0]; ++X)
			{
				for (int32 Y = PayloadInfo.StartIdx[1]; Y <= PayloadInfo.EndIdx[1]; ++Y)
				{
					for (int32 Z = PayloadInfo.StartIdx[2]; Z <= PayloadInfo.EndIdx[2]; ++Z)
					{
						TArray<FCellElement>& Elems = MElements(X, Y, Z);
						int32 ElemIdx = 0;
						for (FCellElement& Elem : Elems)
						{
							if (Elem.Payload == Payload)
							{
								Elems.RemoveAtSwap(ElemIdx);
								break;
							}
							++ElemIdx;
						}
					}
				}
			}
		}
		else
		{
			//TODO: should we skip this if dirty and still dirty?
			auto LastPayload = MDirtyElements.Last().Payload;
			if (!(LastPayload == Payload))
			{
				MPayloadInfo.FindChecked(LastPayload).DirtyPayloadIdx = PayloadInfo.DirtyPayloadIdx;
			}
			MDirtyElements.RemoveAtSwap(PayloadInfo.DirtyPayloadIdx);
		}
	}

	void AddElementToExistingGrid(const TPayloadType& Payload, FPayloadInfo& PayloadInfo, const TBox<T, d>& NewBounds, bool bHasBounds)
	{
		bool bTooBig = false;
		if (bHasBounds)
		{
			if (NewBounds.Extents().Max() > MaxPayloadBounds)
			{
				bTooBig = true;
				bHasBounds = false;
			}
		}

		if(bHasBounds)
		{
			bool bDirty = bIsEmpty;
			TVector<int32, 3> StartIndex;
			TVector<int32, 3> EndIndex;

			if (bIsEmpty == false)
			{
				//add payload to appropriate cells
				StartIndex = MGrid.Cell(NewBounds.Min());
				EndIndex = MGrid.Cell(NewBounds.Max());

				for (int Axis = 0; Axis < d; ++Axis)
				{
					if (StartIndex[Axis] < 0 || EndIndex[Axis] >= MGrid.Counts()[Axis])
					{
						bDirty = true;
					}
				}
			}

			PayloadInfo.GlobalPayloadIdx = INDEX_NONE;

			if (!bDirty)
			{
				PayloadInfo.DirtyPayloadIdx = INDEX_NONE;
				PayloadInfo.StartIdx = StartIndex;
				PayloadInfo.EndIdx = EndIndex;

				for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
				{
					for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
					{
						for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
						{
							MElements(x, y, z).Add({ NewBounds, Payload, StartIndex, EndIndex });
						}
					}
				}
			}
			else
			{
				PayloadInfo.DirtyPayloadIdx = MDirtyElements.Num();
				MDirtyElements.Add({ NewBounds, Payload });
			}
		}
		else
		{
			PayloadInfo.GlobalPayloadIdx = MGlobalPayloads.Num();
			PayloadInfo.DirtyPayloadIdx = INDEX_NONE;
			MGlobalPayloads.Add({ Payload, bTooBig ? NewBounds : TBox<T,d>(TVector<T,d>(TNumericLimits<T>::Lowest()), TVector<T,d>(TNumericLimits<T>::Max())) });
		}
	}

	TArray<TPayloadType> FindAllIntersectionsHelper(const TBox<T, d>& ObjectBox) const
	{
		TArray<TPayloadType> Intersections;
		const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
		const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
		for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
		{
			for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
			{
				for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
				{
					const TArray<FCellElement>& CellElements = MElements(x, y, z);
					Intersections.Reserve(Intersections.Num() + CellElements.Num());
					for (const FCellElement& Elem : CellElements)
					{
						if (ObjectBox.Intersects(Elem.Bounds))
						{
							Intersections.Add(Elem.Payload);
						}
					}
				}
			}
		}


		Algo::Sort(Intersections);

		for (int32 i = Intersections.Num() - 1; i > 0; i--)
		{
			if (Intersections[i] == Intersections[i - 1])
			{
				Intersections.RemoveAtSwap(i, 1, false);
			}
		}

		return Intersections;
	}

	/** Similar to a TSet but acts specifically on grids */
	struct FGridSet
	{
		FGridSet(TVector<int32, 3> Size)
			: NumX(Size[0])
			, NumY(Size[1])
			, NumZ(Size[2])
		{
			int32 BitsNeeded = NumX * NumY * NumZ;
			int32 BytesNeeded = 1 + (BitsNeeded) / 8;
			Data = new uint8[BytesNeeded];
			FMemory::Memzero(Data, BytesNeeded);
		}

		bool Contains(const TVector<int32, 3>& Coordinate)
		{
			//Most sweeps are straight down the Z so store as adjacent Z, then Y, then X
			int32 Idx = (NumY * NumZ) * Coordinate[0] + (NumZ * Coordinate[1]) + Coordinate[2];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			bool bContains = (Data[ByteIdx] >> BitIdx) & 0x1;
			return bContains;
		}

		void Add(const TVector<int32, 3>& Coordinate)
		{
			//Most sweeps are straight down the Z so store as adjacent Z, then Y, then X
			int32 Idx = (NumY * NumZ) * Coordinate[0] + (NumZ * Coordinate[1]) + Coordinate[2];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			uint8 Mask = 1 << BitIdx;
			Data[ByteIdx] |= Mask;
		}

		~FGridSet()
		{
			delete[] Data;
		}

	private:
		int32 NumX;
		int32 NumY;
		int32 NumZ;
		uint8* Data;
	};

	TArray<TPayloadBoundsElement<TPayloadType, T>> MGlobalPayloads;
	TUniformGrid<T, d> MGrid;
	TArrayND<TArray<FCellElement>, d> MElements;
	TArray<FCellElement> MDirtyElements;
	TMap<TPayloadType, FPayloadInfo> MPayloadInfo;
	T MaxPayloadBounds;
	bool bIsEmpty;
};

template<typename TPayloadType, class T, int d>
FArchive& operator<<(FArchive& Ar, TBoundingVolume<TPayloadType, T, d>& BoundingVolume)
{
	check(false);
	return Ar;
}

template<typename TPayloadType, class T, int d>
FArchive& operator<<(FChaosArchive& Ar, TBoundingVolume<TPayloadType, T, d>& BoundingVolume)
{
	BoundingVolume.Serialize(Ar);
	return Ar;
}

}

