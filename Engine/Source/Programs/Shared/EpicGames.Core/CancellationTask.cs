// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Wrapper class to create a waitable task out of a cancellation token
	/// </summary>
	public class CancellationTask : IDisposable
	{
		/// <summary>
		/// Completion source for the task
		/// </summary>
		readonly TaskCompletionSource<bool> CompletionSource;

		/// <summary>
		/// Registration handle with the cancellation token
		/// </summary>
		readonly CancellationTokenRegistration Registration;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Token">The cancellation token to register with</param>
		public CancellationTask(CancellationToken Token)
		{
			CompletionSource = new TaskCompletionSource<bool>();
			Registration = Token.Register(() => CompletionSource.TrySetResult(true));
		}

		/// <summary>
		/// The task that can be waited on
		/// </summary>
		public Task Task
		{
			get { return CompletionSource.Task; }
		}

		/// <summary>
		/// Dispose of any allocated resources
		/// </summary>
		public void Dispose()
		{
			Registration.Dispose();
		}
	}
}
