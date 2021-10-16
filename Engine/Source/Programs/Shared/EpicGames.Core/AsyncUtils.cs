// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility functions for manipulating async tasks
	/// </summary>
	public static class AsyncUtils
	{
		/// <summary>
		/// Converts a cancellation token to a waitable task
		/// </summary>
		/// <param name="Token">Cancellation token</param>
		/// <returns></returns>
		public static Task AsTask(this CancellationToken Token)
		{
			return Task.Delay(-1, Token).ContinueWith(x => { });
		}

		/// <summary>
		/// Waits for a time period to elapse or the task to be cancelled, without throwing an cancellation exception
		/// </summary>
		/// <param name="Time">Time to wait</param>
		/// <param name="Token">Cancellation token</param>
		/// <returns></returns>
		public static Task DelayNoThrow(TimeSpan Time, CancellationToken Token)
		{
			return Task.Delay(Time, Token).ContinueWith(x => { });
		}

		/// <summary>
		/// Removes all the complete tasks from a list, allowing each to throw exceptions as necessary
		/// </summary>
		/// <param name="Tasks">List of tasks to remove tasks from</param>
		public static void RemoveCompleteTasks(this List<Task> Tasks)
		{
			List<Exception> Exceptions = new List<Exception>();

			int OutIdx = 0;
			for (int Idx = 0; Idx < Tasks.Count; Idx++)
			{
				if (Tasks[Idx].IsCompleted)
				{
					AggregateException? Exception = Tasks[Idx].Exception;
					if (Exception != null)
					{
						Exceptions.AddRange(Exception.InnerExceptions);
					}
				}
				else
				{
					if (Idx != OutIdx)
					{
						Tasks[OutIdx] = Tasks[Idx];
					}
					OutIdx++;
				}
			}
			Tasks.RemoveRange(OutIdx, Tasks.Count - OutIdx);

			if (Exceptions.Count > 0)
			{
				throw new AggregateException(Exceptions);
			}
		}

		/// <summary>
		/// Removes all the complete tasks from a list, allowing each to throw exceptions as necessary
		/// </summary>
		/// <param name="Tasks">List of tasks to remove tasks from</param>
		/// <returns>Return values from the completed tasks</returns>
		public static List<T> RemoveCompleteTasks<T>(this List<Task<T>> Tasks)
		{
			List<T> Results = new List<T>();

			int OutIdx = 0;
			for (int Idx = 0; Idx < Tasks.Count; Idx++)
			{
				if (Tasks[Idx].IsCompleted)
				{
					Results.Add(Tasks[Idx].Result);
				}
				else if (Idx != OutIdx)
				{
					Tasks[OutIdx++] = Tasks[Idx];
				}
			}
			Tasks.RemoveRange(OutIdx, Tasks.Count - OutIdx);

			return Results;
		}
	}
}
