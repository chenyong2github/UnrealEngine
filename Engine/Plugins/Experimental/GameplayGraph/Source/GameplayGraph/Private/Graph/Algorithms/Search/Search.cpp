// Copyright Epic Games, Inc. All Rights Reserved.
#include "Graph/Algorithms/Search/Search.h"

#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Graph/GraphVertex.h"

namespace Graph::Algorithms
{
	namespace
	{
		using BFSDataStructure = TQueue<FGraphVertexHandle>;
		using DFSDataStructure = TArray<FGraphVertexHandle>;

		template<template<typename> typename TDataStructure>
		FGraphVertexHandle GetNextAndAdvance(TDataStructure<FGraphVertexHandle>& Queue)
		{
			if constexpr (std::is_same_v<TDataStructure<FGraphVertexHandle>, BFSDataStructure>)
			{
				FGraphVertexHandle Ret;
				if (ensure(Queue.Dequeue(Ret)))
				{
					return Ret;
				}
				return {};
			}
			else if constexpr (std::is_same_v<TDataStructure<FGraphVertexHandle>, DFSDataStructure>)
			{
				if (ensure(!Queue.IsEmpty()))
				{
					FGraphVertexHandle Next = Queue.Top();
					Queue.Pop();
					return Next;
				}

				return {};
			}
			else
			{
				static_assert("Invalid data structure for graph search [GetNextAndAdvance].");
			}
		}

		template<template<typename> typename TDataStructure>
		void AddToWorkQueue(TDataStructure<FGraphVertexHandle>& Queue, const FGraphVertexHandle& Handle)
		{
			if constexpr (std::is_same_v<TDataStructure<FGraphVertexHandle>, BFSDataStructure>)
			{
				Queue.Enqueue(Handle);
			}
			else if constexpr (std::is_same_v<TDataStructure<FGraphVertexHandle>, DFSDataStructure>)
			{
				Queue.Add(Handle);
			}
			else
			{
				static_assert("Invalid data structure for graph search [AddToWorkQueue].");
			}
		}

		template<template<typename> typename TDataStructure>
		FGraphVertexHandle GenericSearch(const FGraphVertexHandle& Start, FSearchCallback Callback)
		{
			TDataStructure<FGraphVertexHandle> WorkQueue;
			AddToWorkQueue<TDataStructure>(WorkQueue, Start);

			TSet<FGraphVertexHandle> Seen;
			Seen.Add(Start);

			while (!WorkQueue.IsEmpty())
			{
				FGraphVertexHandle Next = GetNextAndAdvance<TDataStructure>(WorkQueue);
				if (!Next.IsComplete())
				{
					continue;
				}

				if (Callback(Next))
				{
					return Next;
				}

				// Get neighbors and add to the queue.
				if (TObjectPtr<UGraphVertex> Node = Next.GetVertex())
				{
					Node->ForEachAdjacentVertex(
						[&WorkQueue, &Seen](const FGraphVertexHandle& Neighbor)
						{
							if (Neighbor.IsComplete() && !Seen.Contains(Neighbor))
							{
								AddToWorkQueue<TDataStructure>(WorkQueue, Neighbor);
								Seen.Add(Neighbor);
							}
						}
					);
				}
			}

			return {};
		}

	}

	FGraphVertexHandle BFS(const FGraphVertexHandle& Start, FSearchCallback Callback)
	{
		return GenericSearch<TQueue>(Start, Callback);
	}

	FGraphVertexHandle DFS(const FGraphVertexHandle& Start, FSearchCallback Callback)
	{
		return GenericSearch<TArray>(Start, Callback);
	}
}