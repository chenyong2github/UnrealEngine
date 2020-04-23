// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationOctree.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavigationSystem.h"


//----------------------------------------------------------------------//
// FNavigationOctree
//----------------------------------------------------------------------//
FNavigationOctree::FNavigationOctree(const FVector& Origin, float Radius)
	: TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics>(Origin, Radius)
	, DefaultGeometryGatheringMode(ENavDataGatheringMode::Instant)
	, bGatherGeometry(false)
	, NodesMemory(0)
{
	INC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
}

FNavigationOctree::~FNavigationOctree()
{
	DEC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
	DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, NodesMemory);
	
	ObjectToOctreeId.Empty();
}

void FNavigationOctree::SetDataGatheringMode(ENavDataGatheringModeConfig Mode)
{
	check(Mode != ENavDataGatheringModeConfig::Invalid);
	DefaultGeometryGatheringMode = ENavDataGatheringMode(Mode);
}

void FNavigationOctree::SetNavigableGeometryStoringMode(ENavGeometryStoringMode NavGeometryMode)
{
	bGatherGeometry = (NavGeometryMode == FNavigationOctree::StoreNavGeometry);
}

void FNavigationOctree::DemandLazyDataGathering(FNavigationRelevantData& ElementData)
{
	UObject* ElementOb = ElementData.GetOwner();
	if (ElementOb == nullptr)
	{
		return;
	}

	bool bShrink = false;
	const int32 OrgElementMemory = ElementData.GetGeometryAllocatedSize();

	if (ElementData.IsPendingLazyGeometryGathering() == true && ElementData.SupportsGatheringGeometrySlices() == false)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMeshGenerator_LazyGeometryExport);
		UActorComponent& ActorComp = *CastChecked<UActorComponent>(ElementOb);
		ComponentExportDelegate.ExecuteIfBound(&ActorComp, ElementData);

		bShrink = true;

		// mark this element as no longer needing geometry gathering
		ElementData.bPendingLazyGeometryGathering = false;
	}

	if (ElementData.IsPendingLazyModifiersGathering())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMeshGenerator_LazyModifiersExport);
		INavRelevantInterface* NavElement = Cast<INavRelevantInterface>(ElementOb);
		check(NavElement);
		NavElement->GetNavigationData(ElementData);
		ElementData.bPendingLazyModifiersGathering = false;
		bShrink = true;
	}

	if (bShrink)
	{
		// validate exported data
		// shrink arrays before counting memory
		// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
		ElementData.ValidateAndShrink();
	}

	const int32 ElementMemoryChange = ElementData.GetGeometryAllocatedSize() - OrgElementMemory;
	const_cast<FNavigationOctree*>(this)->NodesMemory += ElementMemoryChange;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemoryChange);
}

void FNavigationOctree::DemandChildLazyDataGathering(FNavigationRelevantData& ElementData, INavRelevantInterface& ChildNavInterface)
{
	if (IsLazyGathering(ChildNavInterface))
	{
		ChildNavInterface.GetNavigationData(ElementData);
		ElementData.ValidateAndShrink();
	}
}

bool FNavigationOctree::IsLazyGathering(const INavRelevantInterface& ChildNavInterface) const
{
	const ENavDataGatheringMode GatheringMode = ChildNavInterface.GetGeometryGatheringMode();
	const bool bDoInstantGathering = ((GatheringMode == ENavDataGatheringMode::Default && DefaultGeometryGatheringMode == ENavDataGatheringMode::Instant)
		|| GatheringMode == ENavDataGatheringMode::Instant);

	return !bDoInstantGathering;
}

void FNavigationOctree::AddNode(UObject* ElementOb, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Element)
{
	// we assume NavElement is ElementOb already cast
	Element.Bounds = Bounds;	

	if (NavElement)
	{
		const bool bDoInstantGathering = !IsLazyGathering(*NavElement);

		if (bGatherGeometry)
		{
			UActorComponent* ActorComp = Cast<UActorComponent>(ElementOb);
			if (ActorComp)
			{
				if (bDoInstantGathering)
				{
					ComponentExportDelegate.ExecuteIfBound(ActorComp, *Element.Data);
				}
				else
				{
					Element.Data->bPendingLazyGeometryGathering = true;
					Element.Data->bSupportsGatheringGeometrySlices = NavElement && NavElement->SupportsGatheringGeometrySlices();
				}
			}
		}

		SCOPE_CYCLE_COUNTER(STAT_Navigation_GatheringNavigationModifiersSync);
		if (bDoInstantGathering)
		{
			NavElement->GetNavigationData(*Element.Data);
		}
		else
		{
			Element.Data->bPendingLazyModifiersGathering = true;
		}
	}

	// validate exported data
	// shrink arrays before counting memory
	// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
	Element.ValidateAndShrink();

	const int32 ElementMemory = Element.GetAllocatedSize();
	NodesMemory += ElementMemory;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemory);

	AddElement(Element);
}

void FNavigationOctree::AppendToNode(const FOctreeElementId2& Id, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Element)
{
	FNavigationOctreeElement OrgData = GetElementById(Id);

	Element = OrgData;
	Element.Bounds = Bounds + OrgData.Bounds.GetBox();

	if (NavElement)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_GatheringNavigationModifiersSync);
		const bool bDoInstantGathering = !IsLazyGathering(*NavElement);

		if (bDoInstantGathering)
		{
			NavElement->GetNavigationData(*Element.Data);
		}
		else
		{
			Element.Data->bPendingChildLazyModifiersGathering = true;
		}
	}

	// validate exported data
	// shrink arrays before counting memory
	// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
	Element.ValidateAndShrink();

	const int32 OrgElementMemory = OrgData.GetAllocatedSize();
	const int32 NewElementMemory = Element.GetAllocatedSize();
	const int32 MemoryDelta = NewElementMemory - OrgElementMemory;

	NodesMemory += MemoryDelta;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, MemoryDelta);

	RemoveElement(Id);
	AddElement(Element);
}

void FNavigationOctree::UpdateNode(const FOctreeElementId2& Id, const FBox& NewBounds)
{
	FNavigationOctreeElement ElementCopy = GetElementById(Id);
	RemoveElement(Id);
	ElementCopy.Bounds = NewBounds;
	AddElement(ElementCopy);
}

void FNavigationOctree::RemoveNode(const FOctreeElementId2& Id)
{
	const FNavigationOctreeElement& Element = GetElementById(Id);
	const int32 ElementMemory = Element.GetAllocatedSize();
	NodesMemory -= ElementMemory;
	DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemory);

	RemoveElement(Id);
}

const FNavigationRelevantData* FNavigationOctree::GetDataForID(const FOctreeElementId2& Id) const
{
	if (Id.IsValidId() == false)
	{
		return nullptr;
	}

	const FNavigationOctreeElement& OctreeElement = GetElementById(Id);

	return &*OctreeElement.Data;
}

void FNavigationOctree::SetElementIdImpl(const uint32 OwnerUniqueId, FOctreeElementId2 Id)
{
	ObjectToOctreeId.Add(OwnerUniqueId, Id);
}

//----------------------------------------------------------------------//
// FNavigationOctreeSemantics
//----------------------------------------------------------------------//
#if NAVSYS_DEBUG
FORCENOINLINE
#endif // NAVSYS_DEBUG
void FNavigationOctreeSemantics::SetElementId(FNavigationOctreeSemantics::FOctree& OctreeOwner, const FNavigationOctreeElement& Element, FOctreeElementId2 Id)
{
	((FNavigationOctree&)OctreeOwner).SetElementIdImpl(Element.OwnerUniqueId, Id);
}
