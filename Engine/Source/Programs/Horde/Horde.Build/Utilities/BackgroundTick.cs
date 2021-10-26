// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Calls a method on a specified interval on a background task
	/// </summary>
	public sealed class BackgroundTick : IAsyncDisposable, IDisposable
	{
		Func<CancellationToken, Task> TickFunc;
		TimeSpan Interval;
		Task BackgroundTask;
		CancellationTokenSource CancellationTokenSource;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TickFunc">The method to call when the interval elapses</param>
		/// <param name="Interval">Inveral between calls</param>
		/// <param name="Logger">Logger for errors produced by the tick function</param>
		public BackgroundTick(Func<CancellationToken, Task> TickFunc, TimeSpan Interval, ILogger Logger)
		{
			this.TickFunc = TickFunc;
			this.Interval = Interval;
			this.CancellationTokenSource = new CancellationTokenSource();
			this.Logger = Logger;
			this.BackgroundTask = Task.Run(BackgroundTickAsync);
		}

		/// <summary>
		/// Signals the tick function to stop executing, and waits for it to complete
		/// </summary>
		public async Task StopAsync()
		{
			CancellationTokenSource.Cancel();
			await BackgroundTask;
		}

		/// <summary>
		/// Background task executor
		/// </summary>
		async Task BackgroundTickAsync()
		{
			CancellationToken CancellationToken = CancellationTokenSource.Token;
			for (; ; )
			{
				try
				{
					Stopwatch Timer = Stopwatch.StartNew();
					await TickFunc(CancellationToken);

					TimeSpan Delay = Interval - Timer.Elapsed;
					if (Delay > TimeSpan.Zero)
					{
						await Task.Delay(Delay, CancellationToken);
					}
				}
				catch (OperationCanceledException) when (CancellationToken.IsCancellationRequested)
				{
					break;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception while executing background task");
				}
			}
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();
			CancellationTokenSource.Dispose();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			StopAsync().Wait();
			CancellationTokenSource.Dispose();
		}
	}
}
