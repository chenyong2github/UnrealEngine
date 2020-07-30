// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundTarjan.h"

namespace Metasound
{
	namespace MetasoundTarjanPrivate
	{
		struct FTarjanVertexInfo
		{
			// Tag value given to a vertex.
			int32 Tag = INDEX_NONE;

			// Lowest tag in strongly connected components
			int32 LowLinkTag = INDEX_NONE;

			// Flag for global stack
			bool bIsOnStack = false;

			// Immediate children of vertex in DAG.
			TArray<int32> Children;
		};

		struct FTarjanAlgoImplSettings 
		{
			// Single vertices can be returned as strongly connected components
			// but usually they are not wanted by the caller. Setting this to
			// true will require all strongly connected components to contain
			// two or more vertices.
			bool bExcludeSingleVertex;
		};

		// Private implementation of algorithm.
		class FTarjanAlgoImpl
		{
			public:

			FTarjanAlgoImpl(const FTarjanAlgoImplSettings& InSettings)
			:	Settings(InSettings)
			{
			}

			bool FindStronglyConnectedComponents(const TSet<FTarjanEdge>& InEdges, TArray<FTarjanStronglyConnectedComponent>& OutComponents)
			{
				using FElement = TMap<int32, FTarjanVertexInfo>::ElementType;

				// Initialize internal data stores and tracking info.
				Init(InEdges);
				
				int32 OriginalNumOutComponents = OutComponents.Num();

				// Iterate through all vertices. Update(...) does a 
				// depth-first-search which may result some vertices getting 
				// inspected during recursive calls to Update(...).
				for (FElement& Element : VertexInfos)
				{
					// Only examine vertice if it hasn't already been inspected.
					if (Element.Value.Tag == INDEX_NONE)
					{
						Update(Element.Key, Element.Value, OutComponents);
					}
				}
				
				// Check if the size of OutComponents has grown.
				return OutComponents.Num() > OriginalNumOutComponents;
			}

			private:

			void Init(const TSet<FTarjanEdge>& InEdges)
			{
				CurrentTag = 0;

				VertexInfos.Reset();
				VertexStack.Reset();

				for (const FTarjanEdge& Edge : InEdges)
				{
					FTarjanVertexInfo& Info = VertexInfos.FindOrAdd(Edge.Get<0>());
					Info.Children.AddUnique(Edge.Get<1>());

					VertexInfos.FindOrAdd(Edge.Get<1>());
				}
			}

			void Update(int32 InVertex, FTarjanVertexInfo& InVertexInfo, TArray<FTarjanStronglyConnectedComponent>& OutComponents)
			{
				InVertexInfo.Tag = CurrentTag;
				InVertexInfo.LowLinkTag = CurrentTag;
				InVertexInfo.bIsOnStack = true;
				VertexStack.Add(InVertex);

				// Tag needs to always be increasing during subsequent calls to
				// Update(...).
				CurrentTag++;

				// Perform depth first traversal through the graph looking for 
				// children that are already on the stack (which denotes a 
				// strongly connected component)
				for (int32 ChildVertex : InVertexInfo.Children)
				{
					FTarjanVertexInfo& ChildInfo = VertexInfos[ChildVertex];

					if (ChildInfo.Tag == INDEX_NONE)
					{
						// If ChildInfo.Tag == INDEX_NONE, then this child is 
						// not on the stack because it has never been inspected.

						// Continue in depth-first-traversal. 
						Update(ChildVertex, ChildInfo, OutComponents);

						// Update to use the lowest link tag found in children.
						InVertexInfo.LowLinkTag = FMath::Min(InVertexInfo.LowLinkTag, ChildInfo.LowLinkTag);
					}
					else if(ChildInfo.bIsOnStack)
					{
						// This child is on the stack and represents a strongly 
						// connected component. 
						InVertexInfo.LowLinkTag = FMath::Min(InVertexInfo.LowLinkTag, ChildInfo.Tag);
					}
				}

				if (InVertexInfo.LowLinkTag == InVertexInfo.Tag)
				{
					// Create a strongly connected component object if this is 
					// the root vertex of a strongly connected component.
					AddStronglyConnectedComponent(InVertex, OutComponents);
				}
			}

			// Adds the current strongly connected component to the 
			// OutComponents array.
			void AddStronglyConnectedComponent(int32 InRootVertex, TArray<FTarjanStronglyConnectedComponent>& OutComponents)
			{
				// When a single vertex represents the entire strongly connected
				// component, the root vertex is the same as the vertex at the
				// top of the stack.
				bool bIsSingleVertex = (VertexStack.Top() == InRootVertex);

				if (bIsSingleVertex && Settings.bExcludeSingleVertex)
				{
					// This strongly connected component is skipped because it 
					// consists of a single vertex. 
					//
					// The stack still needs to be updated as well as the vertex
					// info for the root vertex.
					int32 TopVertex = VertexStack.Pop();	
					FTarjanVertexInfo& TopInfo = VertexInfos[TopVertex];
					TopInfo.bIsOnStack = false;
				}
				else
				{
					bool bDidCompleteLoop = false;

					// Create a new strongly connected component subgraph.
					FTarjanStronglyConnectedComponent& StronglyConnectedComponent = OutComponents.AddDefaulted_GetRef();

					// Pop off the top of the vertex stack until we reach the
					// root vertex.
					while (!bDidCompleteLoop)
					{
						int32 TopVertex = VertexStack.Pop();

						FTarjanVertexInfo& TopInfo = VertexInfos[TopVertex];
						TopInfo.bIsOnStack = false;

						StronglyConnectedComponent.Vertices.Add(TopVertex);

						// The unwinding continues until the current vertex is 
						// removed from the stack.
						bDidCompleteLoop = (TopVertex == InRootVertex);
					}

					// Add the edges associated with the strongly connected
					// component. 
					for (int32 ComponentVertex : StronglyConnectedComponent.Vertices)
					{
						FTarjanVertexInfo& ComponentInfo = VertexInfos[ComponentVertex];

						for (int32 ChildVertex : ComponentInfo.Children)
						{
							if (StronglyConnectedComponent.Vertices.Contains(ChildVertex))
							{
								StronglyConnectedComponent.Edges.Emplace(ComponentVertex, ChildVertex);
							}
						}
					}
				}
			}

			FTarjanAlgoImplSettings Settings;

			int32 CurrentTag = 0;

			TMap<int32, FTarjanVertexInfo> VertexInfos;

			TArray<int32> VertexStack;
		};
	}

	bool FTarjan::FindStronglyConnectedComponents(const TSet<FTarjanEdge>& InEdges, TArray<FTarjanStronglyConnectedComponent>& OutComponents, bool bExcludeSingleVertex)
	{
		using namespace MetasoundTarjanPrivate;

		FTarjanAlgoImplSettings Settings;
		Settings.bExcludeSingleVertex = bExcludeSingleVertex;

		FTarjanAlgoImpl TarjanAlgo(Settings);

		return TarjanAlgo.FindStronglyConnectedComponents(InEdges, OutComponents);
	}

	bool FTarjan::FindStronglyConnectedComponents(const IGraph& InGraph, TArray<FMetasoundStronglyConnectedComponent>& OutComponents, bool bExcludeSingleVertex)
	{
		
		using FNodePair = TTuple<const INode*, const INode*>;

		TArray<const INode*> UniqueNodes;
		TSet<FTarjanEdge> UniqueEdges;

		TMultiMap<FNodePair, FDataEdge> NodePairEdges;

		// To find metasound specific strongly connected components, the nodes
		// and edges must be translated to integers to work with the underlying 
		// algorithm.
		for (const FDataEdge& Edge : InGraph.GetDataEdges())
		{
			if ((nullptr != Edge.From.Node) && (nullptr != Edge.To.Node))
			{
				int32 FromTarjanVertex = UniqueNodes.AddUnique(Edge.From.Node);	
				int32 ToTarjanVertex = UniqueNodes.AddUnique(Edge.To.Node);	

				UniqueEdges.Add(FTarjanEdge(FromTarjanVertex, ToTarjanVertex));

				NodePairEdges.Emplace(FNodePair(Edge.From.Node, Edge.To.Node), Edge);
			}
		}

		TArray<FTarjanStronglyConnectedComponent> StronglyConnectedComponents;

		// Run tarjan on metasound derived graph edges 
		if (FindStronglyConnectedComponents(UniqueEdges, StronglyConnectedComponents, bExcludeSingleVertex))
		{
			// If strongly connected components are found, they must be converted
			// back into metasound types. 
			for (const FTarjanStronglyConnectedComponent& Component : StronglyConnectedComponents)
			{
				FMetasoundStronglyConnectedComponent& MetasoundGraphComponent = OutComponents.AddDefaulted_GetRef();

				for (int32 Vertex : Component.Vertices)
				{
					MetasoundGraphComponent.Nodes.Add(UniqueNodes[Vertex]);
				}

				for (const FTarjanEdge& Edge : Component.Edges)
				{
					TArray<FDataEdge> MetasoundComponentEdges;

					const INode* FromNode = UniqueNodes[Edge.Get<0>()];
					const INode* ToNode = UniqueNodes[Edge.Get<1>()];

					NodePairEdges.MultiFind(FNodePair(FromNode, ToNode), MetasoundComponentEdges);
					MetasoundGraphComponent.Edges.Append(MetasoundComponentEdges);
				}
			}

			return true;
		}

		// No strongly connected components found.
		return false;
	}
}
