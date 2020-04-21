// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenericOctree.h: Generic octree definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GenericOctreePublic.h"
#include "Templates/Models.h"

/** A concise iteration over the children of an octree node. */
#define FOREACH_OCTREE_CHILD_NODE(ChildRef) \
	for(FOctreeChildNodeRef ChildRef(0);!ChildRef.IsNULL();ChildRef.Advance())

class FOctreeChildNodeRef;

/** An unquantized bounding box. */
class FBoxCenterAndExtent
{
public:
	FVector4 Center;
	FVector4 Extent;

	/** Default constructor. */
	FBoxCenterAndExtent() {}

	/** Initialization constructor. */
	FBoxCenterAndExtent(const FVector& InCenter,const FVector& InExtent)
	:	Center(InCenter,0)
	,	Extent(InExtent,0)
	{}

	/** FBox conversion constructor. */
	FBoxCenterAndExtent(const FBox& Box)
	{
		Box.GetCenterAndExtents((FVector&)Center,(FVector&)Extent);
		Center.W = Extent.W = 0;
	}

	/** FBoxSphereBounds conversion constructor. */
	explicit FBoxCenterAndExtent(const FBoxSphereBounds& BoxSphere)
	{
		Center = BoxSphere.Origin;
		Extent = BoxSphere.BoxExtent;
		Center.W = Extent.W = 0;
	}

	/** Center - radius as four contiguous floats conversion constructor. */
	explicit FBoxCenterAndExtent(const float PositionRadius[4])
	{
		Center = FVector(PositionRadius[0],PositionRadius[1],PositionRadius[2]);
		Extent = FVector(PositionRadius[3]);
		Center.W = Extent.W = 0;
	}

	/** Converts to a FBox. */
	FBox GetBox() const
	{
		return FBox(Center - Extent,Center + Extent);
	}

	/**
	 * Determines whether two boxes intersect.
	 * Warning: this operates on the W of the bounds positions!
	 * @return true if the boxes intersect, or false.
	 */
	friend FORCEINLINE bool Intersect(const FBoxCenterAndExtent& A,const FBoxCenterAndExtent& B)
	{
		// CenterDifference is the vector between the centers of the bounding boxes.
		const VectorRegister CenterDifference = VectorAbs(VectorSubtract(VectorLoadAligned(&A.Center),VectorLoadAligned(&B.Center)));

		// CompositeExtent is the extent of the bounding box which is the convolution of A with B.
		const VectorRegister CompositeExtent = VectorAdd(VectorLoadAligned(&A.Extent),VectorLoadAligned(&B.Extent));

		// For each axis, the boxes intersect on that axis if the projected distance between their centers is less than the sum of their
		// extents.  If the boxes don't intersect on any of the axes, they don't intersect.
		return VectorAnyGreaterThan(CenterDifference,CompositeExtent) == false;
	}
	/**
	 * Determines whether two boxes intersect.
	 * Warning: this operates on the W of the bounds positions!
	 * @return true if the boxes intersect, or false.
	 */
	friend FORCEINLINE bool Intersect(const FBoxSphereBounds& A,const FBoxCenterAndExtent& B)
	{
		// CenterDifference is the vector between the centers of the bounding boxes.
		const VectorRegister CenterDifference = VectorAbs(VectorSubtract(VectorLoadFloat3_W0(&A.Origin),VectorLoadAligned(&B.Center)));

		// CompositeExtent is the extent of the bounding box which is the convolution of A with B.
		const VectorRegister CompositeExtent = VectorAdd(VectorLoadFloat3_W0(&A.BoxExtent),VectorLoadAligned(&B.Extent));

		// For each axis, the boxes intersect on that axis if the projected distance between their centers is less than the sum of their
		// extents.  If the boxes don't intersect on any of the axes, they don't intersect.
		return VectorAnyGreaterThan(CenterDifference,CompositeExtent) == false;
	}
	/**
	 * Determines whether two boxes intersect.
	 * Warning: this operates on the W of the bounds positions!
	 * @param A box given in center - radius form as four contiguous floats
	 * @return true if the boxes intersect, or false.
	 */
	friend FORCEINLINE bool Intersect(const float A[4],const FBoxCenterAndExtent& B)
	{
		// CenterDifference is the vector between the centers of the bounding boxes.
		const VectorRegister CenterDifference = VectorAbs(VectorSubtract(VectorLoadFloat3_W0(A),VectorLoadAligned(&B.Center)));

		// CompositeExtent is the extent of the bounding box which is the convolution of A with B.
		const VectorRegister CompositeExtent = VectorAdd(VectorSet_W0(VectorLoadFloat1(A+3)),VectorLoadAligned(&B.Extent));

		// For each axis, the boxes intersect on that axis if the projected distance between their centers is less than the sum of their
		// extents.  If the boxes don't intersect on any of the axes, they don't intersect.
		return VectorAnyGreaterThan(CenterDifference,CompositeExtent) == false;
	}
};

/** A reference to a child of an octree node. */
class FOctreeChildNodeRef
{
public:

	union
	{
		struct
		{
			uint32 X : 1;
			uint32 Y : 1;
			uint32 Z : 1;
			uint32 bNULL : 1;
		};
		uint32 Index : 3;
	};

	/** Initialization constructor. */
	FOctreeChildNodeRef(int32 InX,int32 InY,int32 InZ)
	:	X(InX)
	,	Y(InY)
	,	Z(InZ)
	,	bNULL(false)
	{}

	/** Initialized the reference with a child index. */
	FOctreeChildNodeRef(int32 InIndex = 0)
	:	Index(InIndex)
	{
		checkSlow(Index < 8);
		// some compilers do not allow multiple members of a union to be specified in the constructor init list
		bNULL = false;
	}

	/** Advances the reference to the next child node.  If this was the last node remain, sets bInvalid=true. */
	FORCEINLINE void Advance()
	{
		if(Index < 7)
		{
			++Index;
		}
		else
		{
			bNULL = true;
		}
	}

	/** @return true if the reference isn't set. */
	FORCEINLINE bool IsNULL() const
	{
		return bNULL;
	}
};

/** A subset of an octree node's children that intersect a bounding box. */
class FOctreeChildNodeSubset
{
public:

	union
	{
		struct 
		{
			uint32 bPositiveX : 1;
			uint32 bPositiveY : 1;
			uint32 bPositiveZ : 1;
			uint32 bNegativeX : 1;
			uint32 bNegativeY : 1;
			uint32 bNegativeZ : 1;
		};

		struct
		{
			/** Only the bits for the children on the positive side of the splits. */
			uint32 PositiveChildBits : 3;

			/** Only the bits for the children on the negative side of the splits. */
			uint32 NegativeChildBits : 3;
		};

		/** All the bits corresponding to the child bits. */
		uint32 ChildBits : 6;

		/** All the bits used to store the subset. */
		uint32 AllBits;
	};

	/** Initializes the subset to be empty. */
	FOctreeChildNodeSubset()
	:	AllBits(0)
	{}

	/** Initializes the subset to contain a single node. */
	FOctreeChildNodeSubset(FOctreeChildNodeRef ChildRef)
	:	AllBits(0)
	{
		// The positive child bits correspond to the child index, and the negative to the NOT of the child index.
		PositiveChildBits = ChildRef.Index;
		NegativeChildBits = ~ChildRef.Index;
	}

	/** Determines whether the subset contains a specific node. */
	bool Contains(FOctreeChildNodeRef ChildRef) const;
};

/** The context of an octree node, derived from the traversal of the tree. */
class FOctreeNodeContext
{
public:

	/** The node bounds are expanded by their extent divided by LoosenessDenominator. */
	enum { LoosenessDenominator = 16 };

	/** The bounds of the node. */
	FBoxCenterAndExtent Bounds;

	/** The extent of the node's children. */
	float ChildExtent;

	/** The offset of the childrens' centers from the center of this node. */
	float ChildCenterOffset;

	/** Bits used for culling, semantics left up to the caller (except that it is always set to zero at the root). This does not consume storage because it is leftover in the padding.*/
	uint32 InCullBits;

	/** Bits used for culling, semantics left up to the caller (except that it is always set to zero at the root). This does not consume storage because it is leftover in the padding.*/
	uint32 OutCullBits;

	/** Default constructor. */
	FOctreeNodeContext()
	{}

	/** Initialization constructor, this one is used when we done care about the box anymore */
	FOctreeNodeContext(uint32 InInCullBits, uint32 InOutCullBits)
		:	InCullBits(InInCullBits)
		,	OutCullBits(InOutCullBits)
	{
	}

	/** Initialization constructor. */
	FOctreeNodeContext(const FBoxCenterAndExtent& InBounds)
	:	Bounds(InBounds)
	{
		// A child node's tight extents are half its parent's extents, and its loose extents are expanded by 1/LoosenessDenominator.
		const float TightChildExtent = Bounds.Extent.X * 0.5f;
		const float LooseChildExtent = TightChildExtent * (1.0f + 1.0f / (float)LoosenessDenominator);

		ChildExtent = LooseChildExtent;
		ChildCenterOffset = Bounds.Extent.X - LooseChildExtent;
	}

	/** Initialization constructor. */
	FOctreeNodeContext(const FBoxCenterAndExtent& InBounds, uint32 InInCullBits, uint32 InOutCullBits)
		:	Bounds(InBounds)
		,	InCullBits(InInCullBits)
		,	OutCullBits(InOutCullBits)
	{
		// A child node's tight extents are half its parent's extents, and its loose extents are expanded by 1/LoosenessDenominator.
		const float TightChildExtent = Bounds.Extent.X * 0.5f;
		const float LooseChildExtent = TightChildExtent * (1.0f + 1.0f / (float)LoosenessDenominator);

		ChildExtent = LooseChildExtent;
		ChildCenterOffset = Bounds.Extent.X - LooseChildExtent;
	}

	inline VectorRegister GetChildOffsetVec(int i) const
	{
		union MaskType { VectorRegister v;  VectorRegisterInt i; 
#if PLATFORM_HOLOLENS
			MaskType() 
				: v(MakeVectorRegister(0.0f, 0.0f, 0.0f, 0.0f))
				, i(MakeVectorRegisterInt(0, 0, 0, 0))
				{}
#endif
		} Mask;

		Mask.v = MakeVectorRegister(1u, 2u, 4u, 8u);
		VectorRegisterInt X = VectorIntLoad1(&i);
		VectorRegisterInt A = VectorIntAnd(X, Mask.i);
		Mask.i = VectorIntCompareEQ(Mask.i, A);
		return VectorSelect(Mask.v, VectorSetFloat1(ChildCenterOffset), VectorSetFloat1(-ChildCenterOffset));
	}

	/** Child node initialization constructor. */
	inline FOctreeNodeContext GetChildContext(FOctreeChildNodeRef ChildRef) const
	{
		FBoxCenterAndExtent LocalBounds;
		VectorRegister ZeroW = MakeVectorRegister(1.0f, 1.0f, 1.0f, 0.0f);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorAdd(VectorLoadAligned(&Bounds.Center), GetChildOffsetVec(ChildRef.Index))), &LocalBounds.Center);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorSetFloat1(ChildExtent)), &LocalBounds.Extent);
		return FOctreeNodeContext(LocalBounds);
	}

	/** Construct a child context given the child ref. Optimized to remove all LHS. */
	inline void GetChildContext(FOctreeChildNodeRef ChildRef, FOctreeNodeContext* ChildContext) const
	{
		VectorRegister ZeroW = MakeVectorRegister(1.0f, 1.0f, 1.0f, 0.0f);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorAdd(VectorLoadAligned(&Bounds.Center), GetChildOffsetVec(ChildRef.Index))), &ChildContext->Bounds.Center);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorSetFloat1(ChildExtent)), &ChildContext->Bounds.Extent);

		const float TightChildExtent = ChildExtent * 0.5f;
		const float LooseChildExtent = TightChildExtent * (1.0f + 1.0f / (float)LoosenessDenominator);
		ChildContext->ChildExtent = LooseChildExtent;
		ChildContext->ChildCenterOffset = ChildExtent - LooseChildExtent;
	}
	
	/** Child node initialization constructor. */
	inline FOctreeNodeContext GetChildContext(FOctreeChildNodeRef ChildRef, uint32 InInCullBits, uint32 InOutCullBits) const
	{
		FBoxCenterAndExtent LocalBounds;
		VectorRegister ZeroW = MakeVectorRegister(1.0f, 1.0f, 1.0f, 0.0f);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorAdd(VectorLoadAligned(&Bounds.Center), GetChildOffsetVec(ChildRef.Index))), &LocalBounds.Center);
		VectorStoreAligned(VectorMultiply(ZeroW, VectorSetFloat1(ChildExtent)), &LocalBounds.Extent);
		return FOctreeNodeContext(LocalBounds, InInCullBits, InOutCullBits);
	}
	/**
	 * Determines which of the octree node's children intersect with a bounding box.
	 * @param BoundingBox - The bounding box to check for intersection with.
	 * @return A subset of the children's nodes that intersect the bounding box.
	 */
	FOctreeChildNodeSubset GetIntersectingChildren(const FBoxCenterAndExtent& BoundingBox) const;

	/**
	 * Determines which of the octree node's children contain the whole bounding box, if any.
	 * @param BoundingBox - The bounding box to check for intersection with.
	 * @return The octree's node that the bounding box is farthest from the outside edge of, or an invalid node ref if it's not contained
	 *			by any of the children.
	 */
	FOctreeChildNodeRef GetContainingChild(const FBoxCenterAndExtent& BoundingBox) const;
};

CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGenericOctree, Log, All);

/** An octree. */
template<typename ElementType,typename OctreeSemantics>
class TOctree
{
	using ElementArrayType = TArray<ElementType, typename OctreeSemantics::ElementAllocator>;
public:
	using FNodeIndex = uint32;

private:
	struct FNode
	{
		FNodeIndex ChildNodes = INDEX_NONE;
		uint32 InclusiveNumElements = 0;

		bool IsLeaf() const
		{
			return ChildNodes == INDEX_NONE;
		}
	};

	FOctreeNodeContext RootNodeContext;
	TArray<FNode> TreeNodes;
	TArray<FNodeIndex> ParentLinks;
	TArray<ElementArrayType> TreeElements;

	class FFreeList
	{
		struct FSpan
		{
			FNodeIndex Start;
			FNodeIndex End;
		};

		TArray<FSpan> FreeList;

	public:
		FFreeList()
		{
			Reset();
		}

		void Push(FNodeIndex NodeIndex)
		{
			//find the index that points to our right side node
			int Index = 1; //exclude the dummy
			int Size = FreeList.Num() - 1;

			//start with binary search for larger lists
			while (Size > 32)
			{
				const int LeftoverSize = Size % 2;
				Size = Size / 2;

				const int CheckIndex = Index + Size;
				const int IndexIfLess = CheckIndex + LeftoverSize;

				Index = FreeList[CheckIndex].Start > NodeIndex ? IndexIfLess : Index;
			}

			//small size array optimization
			int ArrayEnd = Index + Size;
			while (Index < ArrayEnd)
			{
				if (FreeList[Index].Start < NodeIndex)
				{
					break;
				}
				Index++;
			}

			//can we merge with the right node
			if (Index < FreeList.Num() && FreeList[Index].End + 1 == NodeIndex)
			{
				FreeList[Index].End = NodeIndex;

				//are we filling the gap between the left and right node
				if (FreeList[Index - 1].Start - 1 == NodeIndex)
				{
					FreeList[Index - 1].Start = FreeList[Index].Start;
					FreeList.RemoveAt(Index);
				}
				return;
			}

			//can we merge with the left node
			if (FreeList[Index - 1].Start - 1 == NodeIndex)
			{
				FreeList[Index - 1].Start = NodeIndex;
				return;
			}

			//we are node that could not be merged
			FreeList.Insert(FSpan{ NodeIndex , NodeIndex }, Index);
		}

		FNodeIndex Pop()
		{
			FSpan& Span = FreeList.Last();
			FNodeIndex Index = Span.Start;
			checkSlow(Index != INDEX_NONE);
			if (Span.Start == Span.End)
			{
				FreeList.Pop();
				return Index;
			}
			else
			{
				Span.Start++;
				return Index;
			}
		}

		void Reset()
		{
			FreeList.Reset(1);
			//push a dummy
			FreeList.Push(FSpan{ INDEX_NONE, INDEX_NONE });
		}

		int Num() const
		{
			//includes one dummy
			return FreeList.Num() - 1;
		}
	};

	TArray<FNodeIndex> FreeList;
	/** The extent of a leaf at the maximum allowed depth of the tree. */
	float MinLeafExtent;

	FNodeIndex AllocateEightNodes()
	{
		FNodeIndex Index = INDEX_NONE;
		if (FreeList.Num())
		{
			Index = (FreeList.Pop() * 8) + 1;
		}
		else
		{
			Index = TreeNodes.AddDefaulted(8);
			ParentLinks.AddDefaulted();
			FNodeIndex ElementIndex = TreeElements.AddDefaulted(8);
			checkSlow(Index == ElementIndex);
		}
		return Index;
	}

	void FreeEightNodes(FNodeIndex Index)
	{
		checkSlow(Index != INDEX_NONE && Index != 0);
		for (int i = 0; i < 8; i++)
		{
			TreeNodes[Index + i] = FNode();
			checkSlow(TreeElements[Index + i].Num() == 0);
		}
		ParentLinks[(Index - 1) / 8] = INDEX_NONE;
		//TODO shrink the TreeNodes as well as the TreeElements and ParentLinks to reduce high watermark memory footprint.
		FreeList.Push((Index - 1) / 8);
	}

	void AddElementInternal(FNodeIndex CurrentNodeIndex, const FOctreeNodeContext& NodeContext, const FBoxCenterAndExtent& ElementBounds, typename TCallTraits<ElementType>::ConstReference Element, ElementArrayType& TempElementStorage)
	{
		checkSlow(CurrentNodeIndex != INDEX_NONE);
		TreeNodes[CurrentNodeIndex].InclusiveNumElements++;
		if (TreeNodes[CurrentNodeIndex].IsLeaf())
		{
			if (TreeElements[CurrentNodeIndex].Num() + 1 > OctreeSemantics::MaxElementsPerLeaf && NodeContext.Bounds.Extent.X > MinLeafExtent)
			{
				TempElementStorage = MoveTemp(TreeElements[CurrentNodeIndex]);

				FNodeIndex ChildStartIndex = AllocateEightNodes();
				ParentLinks[(ChildStartIndex - 1) / 8] = CurrentNodeIndex;
				TreeNodes[CurrentNodeIndex].ChildNodes = ChildStartIndex;
				TreeNodes[CurrentNodeIndex].InclusiveNumElements = 0;

				for (typename TCallTraits<ElementType>::ConstReference ChildElement : TempElementStorage)
				{
					const FBoxCenterAndExtent ChildElementBounds(OctreeSemantics::GetBoundingBox(ChildElement));
					AddElementInternal(CurrentNodeIndex, NodeContext, ChildElementBounds, ChildElement, TempElementStorage);
				}

				TempElementStorage.Reset();
				AddElementInternal(CurrentNodeIndex, NodeContext, ElementBounds, Element, TempElementStorage);
				return;
			}
			else
			{
				int ElementIndex = TreeElements[CurrentNodeIndex].Emplace(Element);

				SetElementId(Element, FOctreeElementId(CurrentNodeIndex, ElementIndex));	
				return;
			}
		}
		else
		{
			const FOctreeChildNodeRef ChildRef = NodeContext.GetContainingChild(ElementBounds);
			if (ChildRef.IsNULL())
			{
				int ElementIndex = TreeElements[CurrentNodeIndex].Emplace(Element);
				SetElementId(Element, FOctreeElementId(CurrentNodeIndex, ElementIndex));
				return;
			}
			else
			{
				FNodeIndex ChildNodeIndex = TreeNodes[CurrentNodeIndex].ChildNodes + ChildRef.Index;
				FOctreeNodeContext ChildNodeContext = NodeContext.GetChildContext(ChildRef);
				AddElementInternal(ChildNodeIndex, ChildNodeContext, ElementBounds, Element, TempElementStorage);
				return;
			}
		}
	}

	void CollapseNodesInternal(FNodeIndex CurrentNodeIndex, ElementArrayType& CollapsedNodeElements)
	{
		CollapsedNodeElements.Append(MoveTemp(TreeElements[CurrentNodeIndex]));	
		TreeElements[CurrentNodeIndex].Reset();

		if (!TreeNodes[CurrentNodeIndex].IsLeaf())
		{
			FNodeIndex ChildStartIndex = TreeNodes[CurrentNodeIndex].ChildNodes;
			for (int i = 0; i < 8; i++)
			{
				CollapseNodesInternal(ChildStartIndex + i, CollapsedNodeElements);
			}
			FreeEightNodes(ChildStartIndex);
		}
	}

	template<typename PredicateFunc, typename IterateFunc>
	void FindNodesWithPredicateInternal(FNodeIndex CurrentNodeIndex, const FOctreeNodeContext& NodeContext, const PredicateFunc& Predicate, const IterateFunc& Func) const
	{
		if (TreeNodes[CurrentNodeIndex].InclusiveNumElements > 0)
		{
			if (Predicate(NodeContext.Bounds))
			{
				Func(CurrentNodeIndex);

				if (!TreeNodes[CurrentNodeIndex].IsLeaf())
				{
					FNodeIndex ChildStartIndex = TreeNodes[CurrentNodeIndex].ChildNodes;
					for (int i = 0; i < 8; i++)
					{
						FindNodesWithPredicateInternal(ChildStartIndex + i, NodeContext.GetChildContext(FOctreeChildNodeRef(i)), Predicate, Func);
					}
				}
			}
		}
	}

	template<typename IterateFunc>
	void FindElementsWithBoundsTestInternal(FNodeIndex CurrentNodeIndex, const FOctreeNodeContext& NodeContext, const FBoxCenterAndExtent& BoxBounds, const IterateFunc& Func) const
	{
		if (TreeNodes[CurrentNodeIndex].InclusiveNumElements > 0)
		{
			for (typename TCallTraits<ElementType>::ConstReference Element : TreeElements[CurrentNodeIndex])
			{
				if (Intersect(OctreeSemantics::GetBoundingBox(Element), BoxBounds))
				{
					Func(Element);
				}
			}

			if (!TreeNodes[CurrentNodeIndex].IsLeaf())
			{
				const FOctreeChildNodeSubset IntersectingChildSubset = NodeContext.GetIntersectingChildren(BoxBounds);
				FNodeIndex ChildStartIndex = TreeNodes[CurrentNodeIndex].ChildNodes;
				for (int i = 0; i < 8; i++)
				{
					if(IntersectingChildSubset.Contains(FOctreeChildNodeRef(i)))
					{
						FindElementsWithBoundsTestInternal(ChildStartIndex + i, NodeContext.GetChildContext(FOctreeChildNodeRef(i)), BoxBounds, Func);
					}
				}
			}
		}
	}

	template<typename IterateFunc>
	void FindFirstElementWithBoundsTestInternal(FNodeIndex CurrentNodeIndex, const FOctreeNodeContext& NodeContext, const FBoxCenterAndExtent& BoxBounds, const IterateFunc& Func, bool& ContinueTraversal) const
	{
		if (ContinueTraversal && TreeNodes[CurrentNodeIndex].InclusiveNumElements > 0)
		{
			for (int Index = 0; ContinueTraversal && Index < TreeElements[CurrentNodeIndex].Num(); Index++)
			{
				typename TCallTraits<ElementType>::ConstReference Element = TreeElements[CurrentNodeIndex][Index];
				if (Intersect(OctreeSemantics::GetBoundingBox(Element), BoxBounds))
				{
					ContinueTraversal &= Func(Element);
				}
			}

			if (ContinueTraversal && !TreeNodes[CurrentNodeIndex].IsLeaf())
			{
				const FOctreeChildNodeSubset IntersectingChildSubset = NodeContext.GetIntersectingChildren(BoxBounds);
				FNodeIndex ChildStartIndex = TreeNodes[CurrentNodeIndex].ChildNodes;
				for (int i = 0; i < 8; i++)
				{
					if (IntersectingChildSubset.Contains(FOctreeChildNodeRef(i)))
					{
						FindFirstElementWithBoundsTestInternal(ChildStartIndex + i, NodeContext.GetChildContext(FOctreeChildNodeRef(i)), BoxBounds, Func, ContinueTraversal);
					}
				}
			}
		}
	}

	template<typename IterateFunc>
	void FindNearbyElementsInternal(FNodeIndex CurrentNodeIndex, const FOctreeNodeContext& NodeContext, const FBoxCenterAndExtent& BoxBounds, const IterateFunc& Func) const
	{
		if (TreeNodes[CurrentNodeIndex].InclusiveNumElements > 0)
		{
			for (int Index = 0; Index < TreeElements[CurrentNodeIndex].Num(); Index++)
			{
				Func(TreeElements[CurrentNodeIndex][Index]);
			}

			if (!TreeNodes[CurrentNodeIndex].IsLeaf())
			{
				// Find the child of the current node, if any, that contains the current new point
				FOctreeChildNodeRef ChildRef = NodeContext.GetContainingChild(BoxBounds);
				if (!ChildRef.IsNULL())
				{
					FNodeIndex ChildStartIndex = TreeNodes[CurrentNodeIndex].ChildNodes;
					// If the specified child node exists and contains any match, push it than process it
					if (TreeNodes[ChildStartIndex + ChildRef.Index].InclusiveNumElements > 0)
					{
						FindNearbyElementsInternal(ChildStartIndex + ChildRef.Index, NodeContext.GetChildContext(ChildRef), BoxBounds, Func);
					}
					// If the child node doesn't is a match, it's not worth pursuing any further. In an attempt to find
					// anything to match vs. the new point, process all of the children of the current octree node
					else
					{
						for (int i = 0; i < 8; i++)
						{
							FindNearbyElementsInternal(ChildStartIndex + i, NodeContext.GetChildContext(FOctreeChildNodeRef(i)), BoxBounds, Func);
						}
					}
				}
			}
		}
	}
public:

	/**
	 * this function will call the passed in function for all elements in the Octree in node by node in no specified order.
	 * @param Func - Function to call with each Element.
	 */
	template<typename IterateAllElementsFunc>
	inline void FindAllElements(const IterateAllElementsFunc& Func) const
	{
		for (const ElementArrayType& Elements : TreeElements)
		{
			for (typename TCallTraits<ElementType>::ConstReference Element : Elements)
			{
				Func(Element);
			}
		}
	}

	/**
	 * this function will traverse the Octree starting from the root in depth first order and the predicate can be used to implement custom culling for each node.
	 * @param Predicate - a Function when given the bounds of the currently traversed node that returns true if traversal should continue or false to skip that branch.
	 * @param Func - Function that will receive the node ID which can be stored and later used to get the elements using GetElementsForNode for all nodes that passed the predicate.
	 */
	template<typename PredicateFunc, typename IterateFunc>
	inline void FindNodesWithPredicate(const PredicateFunc& Predicate, const IterateFunc& Func) const
	{
		FindNodesWithPredicateInternal(0, RootNodeContext, Predicate, Func);
	}

	/**
	 * this function will traverse the Octree starting from the root in depth first order and the predicate can be used to implement custom culling for each node.
	 * @param Predicate - a Function when given the bounds of the currently traversed node that returns true if traversal should continue or false to skip that branch.
	 * @param Func - Function to call with each Element for nodes that passed the predicate.
	 */
	template<typename PredicateFunc, typename IterateFunc>
	inline void FindElementsWithPredicate(const PredicateFunc& Predicate, const IterateFunc& Func) const
	{
		FindNodesWithPredicateInternal(0, RootNodeContext, Predicate, [&Func, this](FNodeIndex NodeIndex)
		{
			for (typename TCallTraits<ElementType>::ConstReference Element : TreeElements[NodeIndex])
			{
				Func(Element);
			}
		});
	}

	/**
	 * this function will traverse the Octree using a fast box-box interssection this should be the preferred way of traversing the tree.
	 * @param BoxBounds - the bounds to test if a node is traversed or skipped.
	 * @param Func - Function to call with each Element for nodes that passed bounds test.
	 */
	template<typename IterateBoundsFunc>
	inline void FindElementsWithBoundsTest(const FBoxCenterAndExtent& BoxBounds, const IterateBoundsFunc& Func) const
	{
		FindElementsWithBoundsTestInternal(0, RootNodeContext, BoxBounds, Func);
	}

	/**
	 * this function will traverse the Octree using a fast box-box intersection and aborts traversal as soon as the Element function returns false.
	 * @param BoxBounds - the bounds to test if a node is traversed or skipped.
	 * @param Func - Function to call with each Element for nodes that passed bounds test.
	 */
	template<typename IterateBoundsFunc>
	inline void FindFirstElementWithBoundsTest(const FBoxCenterAndExtent& BoxBounds, const IterateBoundsFunc& Func) const
	{
		bool ContinueTraversal = true;
		FindFirstElementWithBoundsTestInternal(0, RootNodeContext, BoxBounds, Func, ContinueTraversal);
	}

	/**
	* this function will traverse the Octree using a tryint to find nearby nodes that contain any elements.
	* @param Position - the position to look nearby.
	* @param Func - Function to call with each Element for nodes that passed bounds test.
	*/
	template<typename IterateBoundsFunc>
	inline void FindNearbyElements(const FVector& Position, const IterateBoundsFunc& Func) const
	{
		FindNearbyElementsInternal(0, RootNodeContext, FBoxCenterAndExtent(Position, FVector::ZeroVector), Func);
	}


	/**
	 * Adds an element to the octree.
	 * @param Element - The element to add.
	 */
	inline void AddElement(typename TCallTraits<ElementType>::ConstReference Element)
	{
		ElementArrayType TempElementStorage;
		const FBoxCenterAndExtent ElementBounds(OctreeSemantics::GetBoundingBox(Element));
		AddElementInternal(0, RootNodeContext, ElementBounds, Element, TempElementStorage);
	}

	/**
	 * Removes an element from the octree.
	 * @param ElementId - The element to remove from the octree.
	 */
	inline void RemoveElement(FOctreeElementId ElementId)
	{
		checkSlow(ElementId.IsValidId());

		// Remove the element from the node's element list.
		TreeElements[ElementId.NodeIndex].RemoveAtSwap(ElementId.ElementIndex);

		if (ElementId.ElementIndex < TreeElements[ElementId.NodeIndex].Num())
		{
			// Update the external element id for the element that was swapped into the vacated element index.
			SetElementId(TreeElements[ElementId.NodeIndex][ElementId.ElementIndex], ElementId);
		}

		FNodeIndex CollapseNodeIndex = INDEX_NONE;
		{
			// Update the inclusive element counts for the nodes between the element and the root node,
			// and find the largest node that is small enough to collapse.
			FNodeIndex NodeIndex = ElementId.NodeIndex;
			while (true)
			{
				TreeNodes[NodeIndex].InclusiveNumElements--;
				if (TreeNodes[NodeIndex].InclusiveNumElements < OctreeSemantics::MinInclusiveElementsPerNode)
				{
					CollapseNodeIndex = NodeIndex;
				}

				if (NodeIndex == 0)
				{
					break;
				}

				NodeIndex = ParentLinks[(NodeIndex - 1) / 8];			
			}
		}

		// Collapse the largest node that was pushed below the threshold for collapse by the removal.
		if (CollapseNodeIndex != INDEX_NONE && !TreeNodes[CollapseNodeIndex].IsLeaf())
		{
			if (TreeElements[CollapseNodeIndex].Num() < (int32)TreeNodes[CollapseNodeIndex].InclusiveNumElements)
			{
				ElementArrayType TempElementStorage;
				TempElementStorage.Reserve(TreeNodes[CollapseNodeIndex].InclusiveNumElements);
				// Gather the elements contained in this node and its children.
				CollapseNodesInternal(CollapseNodeIndex, TempElementStorage);
				TreeElements[CollapseNodeIndex] = MoveTemp(TempElementStorage);

				for (int ElementIndex = 0; ElementIndex < TreeElements[CollapseNodeIndex].Num(); ElementIndex++)
				{
					// Update the external element id for the element that's being collapsed.
					SetElementId(TreeElements[CollapseNodeIndex][ElementIndex], FOctreeElementId(CollapseNodeIndex, ElementIndex));
				}

				// Mark the node as a leaf.
				TreeNodes[CollapseNodeIndex].ChildNodes = INDEX_NONE;
			}
		}
	}

	/**
	 * this function resets the octree to empty.
	 */
	void Destroy()
	{
		TreeNodes.Reset(1);
		TreeElements.Reset(1);
		FreeList.Reset();
		ParentLinks.Reset();
		TreeNodes.AddDefaulted();
		TreeElements.AddDefaulted();
	}

	/** Accesses an octree element by ID. */
	ElementType& GetElementById(FOctreeElementId ElementId)
	{
		return TreeElements[ElementId.NodeIndex][ElementId.ElementIndex];
	}

	/** Accesses an octree element by ID. */
	const ElementType& GetElementById(FOctreeElementId ElementId) const
	{
		return TreeElements[ElementId.NodeIndex][ElementId.ElementIndex];
	}

	/**
	 * check if a FOctreeElementId is valid.
	 * @param ElementId - The ElementId to check.
	 */
	bool IsValidElementId(FOctreeElementId ElementId) const
	{
		return ElementId.IsValidId() && ElementId.ElementIndex < TreeElements[ElementId.NodeIndex].Num();
	};

	/**
	 * return all elements for a given node.
	 * @param NodeIndex - The the index of the node can be obtained using FindNodesWithPredicate.
	 */
	TArrayView<const ElementType> GetElementsForNode(FNodeIndex NodeIndex) const
	{
		return TreeElements[NodeIndex];
	}

	/** Writes stats for the octree to the log. */
	void DumpStats() const
	{
		int32 NumNodes = 0;
		int32 NumLeaves = 0;
		int32 NumElements = 0;
		int32 MaxElementsPerNode = 0;
		TArray<int32> NodeElementDistribution;

		FindNodesWithPredicateInternal(0, RootNodeContext, [](const FBoxCenterAndExtent&) { return true; }, [&, this](FNodeIndex NodeIndex)
		{
			const int32 CurrentNodeElementCount = GetElementsForNode(NodeIndex).Num();

			NumNodes++;
			if (TreeNodes[NodeIndex].IsLeaf())
			{
				NumLeaves++;
			}

			NumElements += CurrentNodeElementCount;
			MaxElementsPerNode = FMath::Max(MaxElementsPerNode, CurrentNodeElementCount);

			if (CurrentNodeElementCount >= NodeElementDistribution.Num())
			{
				NodeElementDistribution.AddZeroed(CurrentNodeElementCount - NodeElementDistribution.Num() + 1);
			}
			NodeElementDistribution[CurrentNodeElementCount]++;
		});

		UE_LOG(LogGenericOctree, Log, TEXT("Octree overview:"));
		UE_LOG(LogGenericOctree, Log, TEXT("\t%i nodes"), NumNodes);
		UE_LOG(LogGenericOctree, Log, TEXT("\t%i leaves"), NumLeaves);
		UE_LOG(LogGenericOctree, Log, TEXT("\t%i elements"), NumElements);
		UE_LOG(LogGenericOctree, Log, TEXT("\t%i >= elements per node"), MaxElementsPerNode);
		UE_LOG(LogGenericOctree, Log, TEXT("Octree node element distribution:"));
		for (int32 i = 0; i < NodeElementDistribution.Num(); i++)
		{
			if (NodeElementDistribution[i] > 0)
			{
				UE_LOG(LogGenericOctree, Log, TEXT("\tElements: %3i, Nodes: %3i"), i, NodeElementDistribution[i]);
			}
		}
	}

	SIZE_T GetSizeBytes() const
	{
		SIZE_T TotalSizeBytes = TreeNodes.GetAllocatedSize();
		TotalSizeBytes += TreeElements.GetAllocatedSize();
		TotalSizeBytes += TreeNodes[0].InclusiveNumElements * sizeof(ElementType);
		return TotalSizeBytes;
	}

	float GetNodeLevelExtent(int32 Level) const
	{
		const int32 ClampedLevel = FMath::Clamp<uint32>(Level, 0, OctreeSemantics::MaxNodeDepth);
		return RootNodeContext.Bounds.Extent.X * FMath::Pow((1.0f + 1.0f / (float)FOctreeNodeContext::LoosenessDenominator) / 2.0f, ClampedLevel);
	}

	FBoxCenterAndExtent GetRootBounds() const
	{
		return RootNodeContext.Bounds;
	}

	void ShrinkElements()
	{
		for (ElementArrayType& Elements : TreeElements)
		{
			Elements.Shrink();
		}
	}

	/** 
	 * Apply an arbitrary offset to all elements in the tree 
	 * InOffset - offset to apply
	 * bGlobalOctree - hint that this octree is used as a boundless global volume, 
	 *  so only content will be shifted but not origin of the octree
	 */
	void ApplyOffset(const FVector& InOffset, bool bGlobalOctree = false)
	{
		ElementArrayType TempElementStorage;
		TempElementStorage.Reserve(TreeNodes[0].InclusiveNumElements);

		//collect all elements
		CollapseNodesInternal(0, TempElementStorage);
		checkSlow(TreeNodes[0].IsLeaf());
		Destroy();

		if (!bGlobalOctree)
		{
			RootNodeContext.Bounds.Center += FVector4(InOffset, 0.0f);
		}

		// Add all elements from a saved nodes to a new empty octree
		for (const ElementType& Element : TempElementStorage)
		{
			AddElement(Element);
		}
	}

	/** Initialization constructor. */
	TOctree(const FVector& InOrigin,float InExtent)
		: RootNodeContext(FBoxCenterAndExtent(InOrigin, FVector(InExtent, InExtent, InExtent)), 0, 0)
		, MinLeafExtent(InExtent* FMath::Pow((1.0f + 1.0f / (float)FOctreeNodeContext::LoosenessDenominator) / 2.0f, OctreeSemantics::MaxNodeDepth))
	{
		TreeNodes.AddDefaulted();
		TreeElements.AddDefaulted();
	}

	/** DO NOT USE. This constructor is for internal usage only for hot-reload purposes. */
	TOctree() 
	{
		TreeNodes.AddDefaulted();
		TreeElements.AddDefaulted();
	}

private:


	// Concept definition for the new octree semantics, which adds a new TOctree parameter
	struct COctreeSemanticsV2
	{
		template<typename Semantics>
		auto Requires(typename Semantics::FOctree& OctreeInstance, const ElementType& Element, FOctreeElementId Id)
			-> decltype(Semantics::SetElementId(OctreeInstance, Element, Id));
	};

	// Function overload set which calls the V2 version if it's supported or the old version if it's not
	template <typename Semantics>
	typename TEnableIf<!TModels<COctreeSemanticsV2, Semantics>::Value>::Type SetOctreeSemanticsElementId(const ElementType& Element, FOctreeElementId Id)
	{
		Semantics::SetElementId(Element, Id);
	}
	template <typename Semantics>
	typename TEnableIf<TModels<COctreeSemanticsV2, Semantics>::Value>::Type SetOctreeSemanticsElementId(const ElementType& Element, FOctreeElementId Id)
	{
		Semantics::SetElementId(*this, Element, Id);
	}

protected:
	// redirects SetElementId call to the proper implementation
	void SetElementId(const ElementType& Element, FOctreeElementId Id)
	{
		SetOctreeSemanticsElementId<OctreeSemantics>(Element, Id);
	}
};

#include "GenericOctree.inl"
