// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FGraphAStarDefaultPolicy
{
	static const int32 NodePoolSize = 64;
	static const int32 OpenSetSize = 64;
	static const int32 FatalPathLength = 10000;
	static const bool bReuseNodePoolInSubsequentSearches = false;
};

enum EGraphAStarResult
{
	SearchFail,
	SearchSuccess,
	GoalUnreachable,
	InfiniteLoop
};

const int32 NO_COUNT = INT_MAX;

// To get AStar Graph tracing, enable this define
#define ENABLE_GRAPH_ASTAR_LOGGING 0
#if ENABLE_GRAPH_ASTAR_LOGGING
	#define UE_GRAPH_ASTAR_LOG(Verbosity, Format, ...) UE_LOG(LogAStar, Verbosity, Format, __VA_ARGS__)
#else
	#define UE_GRAPH_ASTAR_LOG(...)
#endif

/**
 *	Default A* node class.
 *	Extend this class and pass as a parameter to FGraphAStar for additional functionality
 */
template<typename TGraph>
struct FGraphAStarDefaultNode
{
	typedef typename TGraph::FNodeRef FGraphNodeRef;

	const FGraphNodeRef NodeRef;
	FGraphNodeRef ParentRef;
	float TraversalCost;
	float TotalCost;
	int32 SearchNodeIndex;
	int32 ParentNodeIndex;
	uint8 bIsOpened : 1;
	uint8 bIsClosed : 1;

	FORCEINLINE FGraphAStarDefaultNode(const FGraphNodeRef& InNodeRef)
		: NodeRef(InNodeRef)
		, ParentRef(INDEX_NONE)
		, TraversalCost(FLT_MAX)
		, TotalCost(FLT_MAX)
		, SearchNodeIndex(INDEX_NONE)
		, ParentNodeIndex(INDEX_NONE)
		, bIsOpened(false)
		, bIsClosed(false)
	{}

	FORCEINLINE void MarkOpened() { bIsOpened = true; }
	FORCEINLINE void MarkNotOpened() { bIsOpened = false; }
	FORCEINLINE void MarkClosed() { bIsClosed = true; }
	FORCEINLINE void MarkNotClosed() { bIsClosed = false; }
	FORCEINLINE bool IsOpened() const { return bIsOpened; }
	FORCEINLINE bool IsClosed() const { return bIsClosed; }
};

#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_IMPL( TemplateClass, TemplateClassParameter, ConditionalReturnType, ConditionalFunctionName, DefaultImpl ) \
struct CQuery##ConditionalFunctionName	\
{	\
	template<typename TemplateClass> auto Requires(TemplateClassParameter& Obj) -> decltype(Obj.ConditionalFunctionName());	\
};	\
template <typename TemplateClass> using ReturnType##ConditionalFunctionName = decltype((DeclVal<TemplateClass>().*DeclVal<decltype(&TemplateClass::ConditionalFunctionName)>())()); \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf< TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ReturnType##ConditionalFunctionName<TemplateClass>>::Type ConditionalFunctionName(TemplateClassParameter & Obj) { return Obj.ConditionalFunctionName(); } \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf<!TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ConditionalReturnType>::Type ConditionalFunctionName(TemplateClassParameter& Obj) { return DefaultImpl; }
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION( ConditionalReturnType, ConditionalFunctionName, DefaultImpl )  DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_IMPL( TemplateClass, TemplateClass, ConditionalReturnType, ConditionalFunctionName, DefaultImpl )
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CONST( ConditionalReturnType, ConditionalFunctionName, DefaultImpl ) DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_IMPL( TemplateClass, const TemplateClass, ConditionalReturnType, ConditionalFunctionName, DefaultImpl )

#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM_IMPL( TemplateClass, TemplateClassParameter, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, DefaultImpl) \
struct CQuery##ConditionalFunctionName	\
{	\
	template<typename TemplateClass> auto Requires(TemplateClassParameter& Obj, ConditionalParamType1 Param1) -> decltype(Obj.ConditionalFunctionName(Param1));	\
};	\
template <typename TemplateClass> using ReturnType##ConditionalFunctionName = decltype((DeclVal<TemplateClass>().*DeclVal<decltype(&TemplateClass::ConditionalFunctionName)>())(DeclVal<ConditionalParamType1>())); \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf< TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ReturnType##ConditionalFunctionName<TemplateClass>>::Type ConditionalFunctionName(TemplateClassParameter & Obj, ConditionalParamType1 Param1) { return Obj.ConditionalFunctionName(Param1); } \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf<!TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ConditionalReturnType>::Type ConditionalFunctionName(TemplateClassParameter& Obj, ConditionalParamType1 Param1) { return DefaultImpl; }
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM( ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, DefaultImpl) DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM_IMPL( TemplateClass, TemplateClass, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, DefaultImpl) 
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM_CONST( ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, DefaultImpl) DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM_IMPL( TemplateClass, const TemplateClass, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, DefaultImpl) 

#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_IMPL( TemplateClass, TemplateClassParameter, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, ConditionalParamType2, DefaultImpl) \
struct CQuery##ConditionalFunctionName	\
{	\
	template<typename TemplateClass> auto Requires(TemplateClassParameter& Obj, ConditionalParamType1 Param1, ConditionalParamType2 Param2) -> decltype(Obj.ConditionalFunctionName(Param1,Param2));	\
};	\
template <typename TemplateClass> using ReturnType##ConditionalFunctionName = decltype((DeclVal<TemplateClass>().*DeclVal<decltype(&TemplateClass::ConditionalFunctionName)>())(DeclVal<ConditionalParamType1>(), DeclVal<ConditionalParamType2>())); \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf< TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ReturnType##ConditionalFunctionName<TemplateClass>>::Type ConditionalFunctionName(TemplateClassParameter & Obj, ConditionalParamType1 Param1, ConditionalParamType2 Param2) { return Obj.ConditionalFunctionName(Param1, Param2); } \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf<!TModels<CQuery##ConditionalFunctionName, TemplateClass>::Value, ConditionalReturnType>::Type ConditionalFunctionName(TemplateClassParameter& Obj, ConditionalParamType1 Param1, ConditionalParamType2 Param2) { return DefaultImpl; }
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS( ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, ConditionalParamType2, DefaultImpl) DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_IMPL( TemplateClass, TemplateClass, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, ConditionalParamType2, DefaultImpl)
#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_CONST( ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, ConditionalParamType2, DefaultImpl) DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_IMPL(TemplateClass, const TemplateClass, ConditionalReturnType, ConditionalFunctionName, ConditionalParamType1, ConditionalParamType2, DefaultImpl)

#define DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CUSTOM( ConditionalFunctionName, QueryReturnType, QueryFunctionName, QueryParam, QueryDefaultImpl, QueryImpl) \
struct CQuery##QueryFunctionName	\
{	\
	template<typename TemplateClass> auto Requires(const TemplateClass& Obj) -> decltype(Obj.ConditionalFunctionName());	\
};	\
template <typename TemplateClass> static FORCEINLINE typename TEnableIf< TModels<CQuery##QueryFunctionName, TemplateClass>::Value, QueryReturnType>::Type QueryFunctionName(const TemplateClass& Obj, QueryParam) { return QueryImpl; } \
template <typename TemplateClass> static FORCEINLINE typename TEnableIf<!TModels<CQuery##QueryFunctionName, TemplateClass>::Value, QueryReturnType>::Type QueryFunctionName(const TemplateClass& Obj, QueryParam) { return QueryDefaultImpl; }

template< bool DoRangeCheck>
class FRangeChecklessAllocator : public FDefaultAllocator
{
public:

	/** set to false if you don't want to lose performance on range checks in performance-critical path finding code. */
	enum { RequireRangeCheck = DoRangeCheck };
};
template <> struct TAllocatorTraits<FRangeChecklessAllocator<true>> : TAllocatorTraits<FDefaultAllocator> {};
template <> struct TAllocatorTraits<FRangeChecklessAllocator<false>> : TAllocatorTraits<FDefaultAllocator> {};

/**
 *	Generic graph A* implementation
 *
 *	TGraph holds graph representation. Needs to implement functions:
 *		bool IsValidRef(FNodeRef NodeRef) const;												- returns whether given node identyfication is correct
 *      FNodeRef GetNeighbour(const FSearchNode& NodeRef, const int32 NeighbourIndex) const;		- returns neighbour ref
 *
 *	it also needs to specify node type
 *		FNodeRef		- type used as identification of nodes in the graph
 *
 *	TQueryFilter (FindPath's parameter) filter class is what decides which graph edges can be used and at what cost. It needs to implement following functions:
 *		float GetHeuristicScale() const;														- used as GetHeuristicCost's multiplier
 *		float GetHeuristicCost(const FSearchNode& StartNode, const FSearchNode& EndNode) const;	- estimate of cost from StartNode to EndNode from a search node
 *		float GetTraversalCost(const FSearchNode& StartNode, const FSearchNode& EndNode) const;	- real cost of traveling from StartNode directly to EndNode from a search node
 *		bool IsTraversalAllowed(const FNodeRef NodeA, const FNodeRef NodeB) const;			- whether traversing given edge is allowed from a NodeRef
 *		bool WantsPartialSolution() const;														- whether to accept solutions that do not reach the goal
 *
 *		// Backward compatible methods, please use the FSearchNode version. If the FSearchNode version are implemented, these methods will not be called at all.
 *      FNodeRef GetNeighbour(const FNodeRef NodeRef, const int32 NeighbourIndex) const;		- returns neighbour ref
 *		float GetHeuristicCost(const FNodeRef StartNodeRef, const FNodeRef EndNodeRef) const;	- estimate of cost from StartNode to EndNode from a NodeRef
 *		float GetTraversalCost(const FNodeRef StartNodeRef, const FNodeRef EndNodeRef) const;	- real cost of traveling from StartNode directly to EndNode from a NodeRef
 *
 *		// Optionally implemented methods to parameterize the search
 *		int32 GetNeighbourCount(FNodeRef NodeRef) const;										- returns number of neighbours that the graph node identified with NodeRef has, it is ok if not implemented, the logic will stop calling GetNeighbour once it received an invalid noderef
 *		bool ShouldIgnoreClosedNodes() const;													- whether to revisit closed node or not
 *		bool ShouldIncludeStartNodeInPath() const;												- whether to put the start node in the resulting path
 *		int32 GetMaxSearchNodes() const;														- whether to limit the number of search node to a maximum
 *		float GetCostLimit() constk																- whether to limit the search to a maximum cost
 */
template<typename TGraph, typename Policy = FGraphAStarDefaultPolicy, typename TSearchNode = FGraphAStarDefaultNode<TGraph>, bool DoRangeCheck = false >
struct FGraphAStar
{
	typedef typename TGraph::FNodeRef FGraphNodeRef;
	typedef TSearchNode FSearchNode;

	using FNodeArray = TArray<FSearchNode, FRangeChecklessAllocator<DoRangeCheck>>;
	using FRangeChecklessSetAllocator = TSetAllocator<TSparseArrayAllocator<FRangeChecklessAllocator<DoRangeCheck>, TInlineAllocator<4, FRangeChecklessAllocator<DoRangeCheck>>>, TInlineAllocator<1, FRangeChecklessAllocator<DoRangeCheck>>>;
	using FNodeMap = TMap<FGraphNodeRef, int32, FRangeChecklessSetAllocator>;
	using FIndexArray = TArray<int32, FRangeChecklessAllocator<DoRangeCheck>>;

	struct FNodeSorter
	{
		const FNodeArray& NodePool;

		FNodeSorter(const FNodeArray& InNodePool)
			: NodePool(InNodePool)
		{}

		FORCEINLINE bool operator()(const int32 A, const int32 B) const
		{
			return NodePool[A].TotalCost < NodePool[B].TotalCost;
		}
	};

	struct FNodePool : FNodeArray
	{
		typedef FNodeArray Super;
		FNodeMap NodeMap;

		FNodePool()
		{
			Super::Reserve(Policy::NodePoolSize);
			NodeMap.Reserve(FMath::RoundUpToPowerOfTwo(Policy::NodePoolSize / 4));
		}

		FORCEINLINE FSearchNode& Add(const FSearchNode& SearchNode)
		{
			FSearchNode& NewNode = Super::Emplace_GetRef(SearchNode);
			NewNode.SearchNodeIndex = &NewNode - Super::GetData();
			NodeMap.Add(SearchNode.NodeRef, NewNode.SearchNodeIndex);
			return NewNode;
		}

		FORCEINLINE FSearchNode& FindOrAdd(const FGraphNodeRef NodeRef)
		{
			// first find if node already exist in node map
			const int32 NotInMapIndex = -1;
			int32& Index = NodeMap.FindOrAdd(NodeRef, NotInMapIndex);
			if (Index != NotInMapIndex)
			{
				return (*this)[Index];
			}

			// node not found, add it and setup index in node map
			FSearchNode& NewNode = Super::Emplace_GetRef(NodeRef);
			NewNode.SearchNodeIndex = &NewNode - Super::GetData();
			Index = NewNode.SearchNodeIndex;

			return NewNode;
		}

		FORCEINLINE FSearchNode* Find(const FGraphNodeRef NodeRef)
		{
			const int32* IndexPtr = NodeMap.Find(NodeRef);
			return IndexPtr ? &(*this)[*IndexPtr] : nullptr;
		}

		UE_DEPRECATED(4.15, "This function is now deprecated, please use FindOrAdd instead")
			FSearchNode& Get(const FGraphNodeRef NodeRef)
		{
			return FindOrAdd(NodeRef);
		}

		FORCEINLINE void Reset()
		{
			Super::Reset(Policy::NodePoolSize);
			NodeMap.Reset();
		}

		FORCEINLINE void ReinitNodes()
		{
			for (FSearchNode& Node : *this)
			{
				new (&Node) FSearchNode(Node.NodeRef);
			}
		}
	};

	struct FOpenList : FIndexArray
	{
		typedef FIndexArray Super;
		FNodeArray& NodePool;
		const FNodeSorter& NodeSorter;

		FOpenList(FNodeArray& InNodePool, const FNodeSorter& InNodeSorter)
			: NodePool{ InNodePool }, NodeSorter{ InNodeSorter }
		{
			Super::Reserve(Policy::OpenSetSize);
		}

		void Push(FSearchNode& SearchNode)
		{
			Super::HeapPush(SearchNode.SearchNodeIndex, NodeSorter);
			SearchNode.MarkOpened();
		}

		void Modify(FSearchNode& SearchNode)
		{
			for (int32& NodeIndex : *this)
			{
				if (NodeIndex == SearchNode.SearchNodeIndex)
				{
					AlgoImpl::HeapSiftUp(Super::GetData(), 0, &NodeIndex - Super::GetData(), FIdentityFunctor(), NodeSorter);
					return;
				}
			}
			check(false); // We should never reach here.
		}

		int32 PopIndex(bool bAllowShrinking = true)
		{
			int32 SearchNodeIndex = INDEX_NONE;
			Super::HeapPop(SearchNodeIndex, NodeSorter, /*bAllowShrinking = */false);
			NodePool[SearchNodeIndex].MarkNotOpened();
			return SearchNodeIndex;
		}

		UE_DEPRECATED(4.15, "This function is now deprecated, please use PopIndex instead")
			FSearchNode& Pop(bool bAllowShrinking = true)
		{
			const int32 Index = PopIndex(bAllowShrinking);
			return NodePool[Index];
		}
	};

	const TGraph& Graph;
	FNodePool NodePool;
	FNodeSorter NodeSorter;
	FOpenList OpenList;


	// TGraph optionally implemented wrapper methods
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_1PARAM_CONST(int32, GetNeighbourCount, const FGraphNodeRef, NO_COUNT);
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_CONST(FGraphNodeRef, GetNeighbour, const FSearchNode&, const int32, Obj.GetNeighbour(Param1.NodeRef,Param2));

	// TQueryFilter optionally implemented wrapper methods
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_CONST(float, GetTraversalCost, const FSearchNode&, const FSearchNode&, Obj.GetTraversalCost(Param1.NodeRef, Param2.NodeRef))
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS_CONST(float, GetHeuristicCost, const FSearchNode&, const FSearchNode&, Obj.GetHeuristicCost(Param1.NodeRef, Param2.NodeRef))
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CONST(bool, ShouldIgnoreClosedNodes, false);
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CONST(bool, ShouldIncludeStartNodeInPath, false);
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CONST(float, GetCostLimit, MAX_FLT);
	// Custom methods implemented over TQueryFilter optionally implemented methods
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CUSTOM(GetMaxSearchNodes, bool, HasReachMaxSearchNodes, uint32 NodeCount, false, NodeCount >= Obj.GetMaxSearchNodes());
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_CUSTOM(GetCostLimit, bool, HasExceededCostLimit, float Cost, false, Cost > Obj.GetCostLimit());

	// TResultPathInfo optionally implemented wrapper methods
	DECLARE_OPTIONALLY_IMPLEMENTED_TEMPLATE_CLASS_FUNCTION_2PARAMS(FGraphNodeRef, SetPathInfo, const int32, const FSearchNode&, Obj[Param1] = Param2.NodeRef);

	FGraphAStar(const TGraph& InGraph)
		: Graph{ InGraph }, NodeSorter{ NodePool }, OpenList{ NodePool, NodeSorter }
	{
		NodePool.Reserve(Policy::NodePoolSize);
	}

	/** 
	 * Single run of A* loop: get node from open set and process neighbors 
	 * returns true if loop should be continued
	 */
	template<typename TQueryFilter>
	FORCEINLINE bool ProcessSingleNode(const FSearchNode& EndNode, const bool bIsBound, const TQueryFilter& Filter, int32& OutBestNodeIndex, float& OutBestNodeCost)
	{
		// Pop next best node and put it on closed list
		const int32 ConsideredNodeIndex = OpenList.PopIndex();
		FSearchNode& ConsideredNodeUnsafe = NodePool[ConsideredNodeIndex];
		ConsideredNodeUnsafe.MarkClosed();

		// We're there, store and move to result composition
		if (bIsBound && (ConsideredNodeUnsafe.NodeRef == EndNode.NodeRef))
		{
			OutBestNodeIndex = ConsideredNodeUnsafe.SearchNodeIndex;
			OutBestNodeCost = 0.f;
			return false;
		}

		const float HeuristicScale = Filter.GetHeuristicScale();

		// consider every neighbor of BestNode
		const int32 NeighbourCount = GetNeighbourCount(Graph, ConsideredNodeUnsafe.NodeRef);
		for (int32 NeighbourNodeIndex = 0; NeighbourNodeIndex < NeighbourCount; ++NeighbourNodeIndex)
		{
			const auto& NeighbourRef = GetNeighbour(Graph, NodePool[ConsideredNodeIndex], NeighbourNodeIndex);

			// invalid neigbour check
			if (Graph.IsValidRef(NeighbourRef) == false)
			{
				if(NeighbourCount == NO_COUNT)
				{
					// if user did not implemented the GetNeighbourCount method, let stop at the first invalid neighbour
					break;
				}
				else
				{
					// skipping invalid neighbours
					continue;
				}
			}

			// validate and sanitize
			if (NeighbourRef == NodePool[ConsideredNodeIndex].ParentRef
				|| NeighbourRef == NodePool[ConsideredNodeIndex].NodeRef
				|| Filter.IsTraversalAllowed(NodePool[ConsideredNodeIndex].NodeRef, NeighbourRef) == false)
			{
				UE_GRAPH_ASTAR_LOG(Warning, TEXT("Filtered %lld from %lld"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef);
				continue;
			}

			// check against max search nodes limit
			FSearchNode* ExistingNeighbourNode = nullptr;
			if(HasReachMaxSearchNodes(Filter, (uint32)NodePool.Num()))
			{
				// let's skip this one if it is not already in the NodePool
				ExistingNeighbourNode = NodePool.Find(NeighbourRef);
				if (!ExistingNeighbourNode)
				{
					UE_GRAPH_ASTAR_LOG(Warning, TEXT("Reach Limit %lld from %lld"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef);
					continue;
				}
			}
			FSearchNode& NeighbourNode = ExistingNeighbourNode ? *ExistingNeighbourNode : NodePool.FindOrAdd(NeighbourRef);

			// check condition to avoid search of closed nodes even if they could have lower cost
			if (ShouldIgnoreClosedNodes(Filter) && NeighbourNode.IsClosed())
			{
				UE_GRAPH_ASTAR_LOG(Warning, TEXT("Skipping closed %lld from %lld"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef);
				continue;
			}

			// calculate cost and heuristic.
			const float NewTraversalCost = GetTraversalCost(Filter, NodePool[ConsideredNodeIndex], NeighbourNode) + NodePool[ConsideredNodeIndex].TraversalCost;
			const float NewHeuristicCost = bIsBound && (NeighbourNode.NodeRef != EndNode.NodeRef)
				? (GetHeuristicCost(Filter, NeighbourNode, EndNode) * HeuristicScale)
				: 0.f;
			const float NewTotalCost = NewTraversalCost + NewHeuristicCost;

			// check against cost limit
			if (HasExceededCostLimit(Filter, NewTotalCost))
			{
				UE_GRAPH_ASTAR_LOG(Warning, TEXT("Skipping reach cost limit %lld from %lld cost %f total %f prev cost %f limit %f"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef, NewTraversalCost, NewTotalCost, NeighbourNode.TotalCost, GetCostLimit(Filter));
				continue;
			}

			// check if this is better then the potential previous approach
			if (NewTotalCost >= NeighbourNode.TotalCost)
			{
				// if not, skip
				UE_GRAPH_ASTAR_LOG(Warning, TEXT("Skipping new cost higher %lld from %lld cost %f total %f prev cost %f"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef, NewTraversalCost, NewTotalCost, NeighbourNode.TotalCost);
				continue;
			}

			// fill in
			NeighbourNode.TraversalCost = NewTraversalCost;
			NeighbourNode.TotalCost = NewTotalCost;
			NeighbourNode.ParentRef = NodePool[ConsideredNodeIndex].NodeRef;
			NeighbourNode.ParentNodeIndex = NodePool[ConsideredNodeIndex].SearchNodeIndex;
			NeighbourNode.MarkNotClosed();

			if (NeighbourNode.IsOpened() == false)
			{
			    UE_GRAPH_ASTAR_LOG(Warning, TEXT("Pushing %lld from %lld cost %f total %f"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef, NewTraversalCost, NewTotalCost);
				OpenList.Push(NeighbourNode);
			}
			else
			{
			    UE_GRAPH_ASTAR_LOG(Warning, TEXT("Modifying %lld from %lld cost %f total %f"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef, NewTraversalCost, NewTotalCost);
				OpenList.Modify(NeighbourNode);
			}

			// in case there's no path let's store information on
			// "closest to goal" node
			// using Heuristic cost here rather than Traversal or Total cost
			// since this is what we'll care about if there's no solution - this node 
			// will be the one estimated-closest to the goal
			if (NewHeuristicCost < OutBestNodeCost)
			{
				UE_GRAPH_ASTAR_LOG(Warning, TEXT("New best path %lld from %lld new best heuristic %f prev best heuristic %f"), (uint64)NeighbourRef, (uint64)NodePool[ConsideredNodeIndex].NodeRef, NewHeuristicCost, OutBestNodeCost);
				OutBestNodeCost = NewHeuristicCost;
				OutBestNodeIndex = NeighbourNode.SearchNodeIndex;
			}
		}

		return true;
	}

	/** 
	 *	Performs the actual search.
	 *	@param [OUT] OutPath - on successful search contains a sequence of graph nodes representing 
	 *		solution optimal within given constraints
	 */
	template<typename TQueryFilter, typename TResultPathInfo = TArray<FGraphNodeRef> >
	EGraphAStarResult FindPath(const FSearchNode& StartNode, const FSearchNode& EndNode, const TQueryFilter& Filter, TResultPathInfo& OutPath)
	{
		if (!(Graph.IsValidRef(StartNode.NodeRef) && Graph.IsValidRef(EndNode.NodeRef)))
		{
			return SearchFail;
		}

		if (StartNode.NodeRef == EndNode.NodeRef)
		{
			return SearchSuccess;
		}

		if (Policy::bReuseNodePoolInSubsequentSearches)
		{
			NodePool.ReinitNodes();
		}
		else
		{
			NodePool.Reset();
		}
		OpenList.Reset();

		// kick off the search with the first node
		FSearchNode& StartPoolNode = NodePool.Add(StartNode);
		StartPoolNode.TraversalCost = 0;
		StartPoolNode.TotalCost = GetHeuristicCost(Filter, StartNode, EndNode) * Filter.GetHeuristicScale();

		OpenList.Push(StartPoolNode);

		int32 BestNodeIndex = StartPoolNode.SearchNodeIndex;
		float BestNodeCost = StartPoolNode.TotalCost;

		EGraphAStarResult Result = EGraphAStarResult::SearchSuccess;
		const bool bIsBound = true;
		
		bool bProcessNodes = true;
		while (OpenList.Num() > 0 && bProcessNodes)
		{
			bProcessNodes = ProcessSingleNode(EndNode, bIsBound, Filter, BestNodeIndex, BestNodeCost);
		}

		// check if we've reached the goal
		if (BestNodeCost != 0.f)
		{
			Result = EGraphAStarResult::GoalUnreachable;
		}

		// no point to waste perf creating the path if querier doesn't want it
		if (Result == EGraphAStarResult::SearchSuccess || Filter.WantsPartialSolution())
		{
			// store the path. Note that it will be reversed!
			int32 SearchNodeIndex = BestNodeIndex;
			int32 PathLength = ShouldIncludeStartNodeInPath(Filter) && BestNodeIndex != StartPoolNode.SearchNodeIndex ? 1 : 0;
			do 
			{
				PathLength++;
				SearchNodeIndex = NodePool[SearchNodeIndex].ParentNodeIndex;
			} while (NodePool.IsValidIndex(SearchNodeIndex) && NodePool[SearchNodeIndex].NodeRef != StartPoolNode.NodeRef && ensure(PathLength < Policy::FatalPathLength));
			
			if (PathLength >= Policy::FatalPathLength)
			{
				Result = EGraphAStarResult::InfiniteLoop;
			}

			OutPath.Reset(PathLength);
			OutPath.AddZeroed(PathLength);

			// store the path
			SearchNodeIndex = BestNodeIndex;
			int32 ResultNodeIndex = PathLength - 1;
			do
			{
				SetPathInfo(OutPath, ResultNodeIndex--, NodePool[SearchNodeIndex]);
				SearchNodeIndex = NodePool[SearchNodeIndex].ParentNodeIndex;
			} while (ResultNodeIndex >= 0);
		}
		
		return Result;
	}

	/** Floods node pool until running out of either free nodes or open set  */
	template<typename TQueryFilter>
	EGraphAStarResult FloodFrom(const FSearchNode& StartNode, const TQueryFilter& Filter)
	{
		if (!(Graph.IsValidRef(StartNode.NodeRef)))
		{
			return SearchFail;
		}

		NodePool.Reset();
		OpenList.Reset();

		// kick off the search with the first node
		FSearchNode& StartPoolNode = NodePool.Add(StartNode);
		StartPoolNode.TraversalCost = 0;
		StartPoolNode.TotalCost = 0;

		OpenList.Push(StartPoolNode);

		int32 BestNodeIndex = StartPoolNode.SearchNodeIndex;
		float BestNodeCost = StartPoolNode.TotalCost;
		
		const FSearchNode FakeEndNode = StartNode;
		const bool bIsBound = false;

		bool bProcessNodes = true;
		while (OpenList.Num() > 0 && bProcessNodes)
		{
			bProcessNodes = ProcessSingleNode(FakeEndNode, bIsBound, Filter, BestNodeIndex, BestNodeCost);
		}

		return EGraphAStarResult::SearchSuccess;
	}

	template<typename TQueryFilter>
	bool HasReachMaxSearchNodes(const TQueryFilter& Filter) const
	{
		return HasReachMaxSearchNodes(Filter, (uint32)NodePool.Num());
	}
};