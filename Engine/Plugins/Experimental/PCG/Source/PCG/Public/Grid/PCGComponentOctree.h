// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/BoxSphereBounds.h"
#include "Math/GenericOctreePublic.h"
#include "UObject/ObjectPtr.h"

template <typename ElementType, typename OctreeSemantics> class TOctree2;

class UPCGComponent;

struct PCG_API FPCGComponentOctreeID : public TSharedFromThis<FPCGComponentOctreeID, ESPMode::ThreadSafe>
{
	FOctreeElementId2 Id;
};

typedef TSharedRef<struct FPCGComponentOctreeID, ESPMode::ThreadSafe> FPCGComponentOctreeIDSharedRef;

struct PCG_API FPCGComponentRef
{
	FPCGComponentRef(UPCGComponent* InComponent, const FPCGComponentOctreeIDSharedRef& InIdShared);

	void UpdateBounds();

	FPCGComponentOctreeIDSharedRef IdShared;
	TObjectPtr<UPCGComponent> Component;
	FBoxSphereBounds Bounds;
};

struct PCG_API FPCGComponentRefSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FPCGComponentRef& InVolume)
	{
		return InVolume.Bounds;
	}

	FORCEINLINE static const bool AreElementsEqual(const FPCGComponentRef& A, const FPCGComponentRef& B)
	{
		return A.Component == B.Component;
	}

	FORCEINLINE static void ApplyOffset(FPCGComponentRef& InVolume, const FVector& Offset)
	{
		InVolume.Bounds.Origin += Offset;
	}

	FORCEINLINE static void SetElementId(const FPCGComponentRef& Element, FOctreeElementId2 OctreeElementID)
	{
		Element.IdShared->Id = OctreeElementID;
	}
};

typedef TOctree2<FPCGComponentRef, FPCGComponentRefSemantics> FPCGComponentOctree;

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Math/GenericOctree.h"
#endif
