// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	interface IAsyncDisposer
	{
		void Add(Task Task);
	}

	internal class AsyncDisposer : IAsyncDisposer, IAsyncDisposable
	{
		object LockObject = new object();
		List<Task> Tasks = new List<Task>();
		ILogger Logger;

		public AsyncDisposer(ILogger<AsyncDisposer> Logger)
		{
			this.Logger = Logger;
		}

		public void Add(Task Task)
		{
			Task ContinuationTask = Task.ContinueWith(Remove);
			lock (LockObject)
			{
				Tasks.Add(ContinuationTask);
			}
		}

		private void Remove(Task Task)
		{
			if (Task.IsFaulted)
			{
				Logger.LogError(Task.Exception, "Exception while disposing task");
			}
		}

		public async ValueTask DisposeAsync()
		{
			List<Task> TasksCopy;
			lock (LockObject)
			{
				TasksCopy = new List<Task>(Tasks);
			}

			Task WaitTask = Task.WhenAll(TasksCopy);
			while (!WaitTask.IsCompleted)
			{
				Logger.LogInformation("Waiting for {NumTasks} tasks to complete", TasksCopy.Count);
				await Task.WhenAny(WaitTask, Task.Delay(TimeSpan.FromSeconds(5.0)));
			}
		}
	}
}
