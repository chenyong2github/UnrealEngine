// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Wrapper to allow awaiting an event being signalled, similar to an AutoResetEvent.
	/// </summary>
	public class AsyncEvent
	{
		/// <summary>
		/// Completion source to wait on
		/// </summary>
		TaskCompletionSource<bool> Source = new TaskCompletionSource<bool>();

		/// <summary>
		/// Signal the event
		/// </summary>
		public void Set()
		{
			TaskCompletionSource<bool> PrevSource = Interlocked.Exchange(ref Source, new TaskCompletionSource<bool>());
			PrevSource.SetResult(true);
		}

		/// <summary>
		/// Determines if this event is set
		/// </summary>
		/// <returns>True if the event is set</returns>
		public bool IsSet()
		{
			return Source.Task.IsCompleted;
		}

		/// <summary>
		/// Waits for this event to be set
		/// </summary>
		public Task Task
		{
			get { return Source.Task; }
		}
	}
}
