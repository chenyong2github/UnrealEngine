// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace EpicGames.Core
{
	/// <summary>
	/// Async equivalents for parallel methods
	/// </summary>
	public static class ParallelTask
	{
		/// <summary>
		/// Execute a large number of tasks in parallel over a collection. Parallel.ForEach() method isn't generally compatible with asynchronous programming, because any
		/// exceptions are thrown on the 
		/// </summary>
		/// <param name="FromInclusive">The starting index</param>
		/// <param name="ToExclusive">The last index, exclusive</param>
		/// <param name="Action">Action to perform for each item in the collection</param>
		/// <returns>Async task</returns>
		public static Task ForAsync(int FromInclusive, int ToExclusive, Action<int> Action)
		{
			return ForEachAsync(Enumerable.Range(FromInclusive, ToExclusive - FromInclusive), Action);
		}

		/// <summary>
		/// Execute a large number of tasks in parallel over a collection. Parallel.ForEach() method isn't generally compatible with asynchronous programming, because any
		/// exceptions are thrown on the 
		/// </summary>
		/// <typeparam name="T">The collection type</typeparam>
		/// <param name="Collection">The collection to iterate over</param>
		/// <param name="Action">Action to perform for each item in the collection</param>
		/// <returns>Async task</returns>
		public static Task ForEachAsync<T>(IEnumerable<T> Collection, Action<T> Action)
		{
			ExecutionDataflowBlockOptions Options = new ExecutionDataflowBlockOptions();
			Options.MaxDegreeOfParallelism = DataflowBlockOptions.Unbounded;

			ActionBlock<T> Actions = new ActionBlock<T>(Action, Options);
			foreach (T Item in Collection)
			{
				Actions.Post(Item);
			}
			Actions.Complete();

			return Actions.Completion;
		}

		/// <summary>
		/// Execute a large number of tasks in parallel over a collection. Parallel.ForEach() method isn't generally compatible with asynchronous programming, because any
		/// exceptions are thrown on the 
		/// </summary>
		/// <typeparam name="T">The collection type</typeparam>
		/// <param name="Collection">The collection to iterate over</param>
		/// <param name="Action">Action to perform for each item in the collection</param>
		/// <param name="MaxDegreeOfParallelism">Maximum degree of parallelism</param>
		/// <returns>Async task</returns>
		public static Task ForEachAsync<T>(IEnumerable<T> Collection, Func<T, Task> Action, int MaxDegreeOfParallelism)
		{
			ExecutionDataflowBlockOptions Options = new ExecutionDataflowBlockOptions();
			Options.MaxDegreeOfParallelism = MaxDegreeOfParallelism;

			ActionBlock<T> Actions = new ActionBlock<T>(Action, Options);
			foreach (T Item in Collection)
			{
				Actions.Post(Item);
			}
			Actions.Complete();

			return Actions.Completion;
		}
	}
}
