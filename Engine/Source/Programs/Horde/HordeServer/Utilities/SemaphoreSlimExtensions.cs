// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension method to wrap a semaphore in a using statement
	/// </summary>
	public static class SemaphoreSlimExtensions
	{
		/// <summary>
		/// Returns a disposable semaphore
		/// </summary>
		/// <param name="Semaphore">the semaphore to wrap</param>
		/// <param name="CancelToken">optional cancellation token</param>
		/// <returns></returns>
		public static async Task<IDisposable> UseWaitAsync(this SemaphoreSlim Semaphore, CancellationToken CancelToken = default(CancellationToken))
		{
			await Semaphore.WaitAsync(CancelToken).ConfigureAwait(false);
			return new ReleaseWrapper(Semaphore);
		}

		/// <summary>
		/// Semaphore wrapper implementing IDisposable
		/// </summary>
		private class ReleaseWrapper : IDisposable
		{
			/// <summary>
			/// The semaphore to wrap
			/// </summary>
			private readonly SemaphoreSlim Semaphore;

			/// <summary>
			/// Whether this has been disposed
			/// </summary>
			private bool bIsDisposed;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Semaphore">The semaphore to wrap</param>
			public ReleaseWrapper(SemaphoreSlim Semaphore)
			{
				this.Semaphore = Semaphore;
			}

			/// <summary>
			/// Releases the lock on dispose
			/// </summary>
			public void Dispose()
			{
				if (bIsDisposed)
				{
					return;
				}

				Semaphore.Release();
				bIsDisposed = true;
			}
		}
	}
}
