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
		TaskCompletionSource<bool> _source = new TaskCompletionSource<bool>();

		/// <summary>
		/// Signal the event
		/// </summary>
		public void Set()
		{
			TaskCompletionSource<bool> prevSource = Interlocked.Exchange(ref _source, new TaskCompletionSource<bool>());
			prevSource.SetResult(true);
		}

		/// <summary>
		/// Determines if this event is set
		/// </summary>
		/// <returns>True if the event is set</returns>
		public bool IsSet()
		{
			return _source.Task.IsCompleted;
		}

		/// <summary>
		/// Waits for this event to be set
		/// </summary>
		public Task Task => _source.Task;
	}
}
