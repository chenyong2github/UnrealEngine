// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Base class for a background service which is ticked on a regular schedule
	/// </summary>
	public abstract class TickedBackgroundService : BackgroundService
	{
		/// <summary>
		/// Frequency that the service will be ticked
		/// </summary>
		public TimeSpan Interval { get; set; }

		/// <summary>
		/// Logger interface
		/// </summary>
		private ILogger Logger;

		/// <summary>
		/// Event which can be signalled to make the background service tick immediately
		/// </summary>
		private AsyncEvent TickImmediatelyEvent = new AsyncEvent();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Interval">Interval for ticking the service</param>
		/// <param name="Logger">Logger for debug messages and errors</param>
		public TickedBackgroundService(TimeSpan Interval, ILogger Logger)
		{
			this.Interval = Interval;
			this.Logger = Logger;
		}

		/// <summary>
		/// Ticks the service immediately
		/// </summary>
		protected void TickNow()
		{
			TickImmediatelyEvent.Set();
		}

		/// <inheritdoc/>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		protected sealed override async Task ExecuteAsync(CancellationToken StoppingToken)
		{
			using (CancellationTask StoppingTask = new CancellationTask(StoppingToken))
			{
				// Wait a random amount of time before the first iteration
				await Task.WhenAny(StoppingTask.Task, Task.Delay(new Random().NextDouble() * Interval));

				// Enter the main tick loop
				Stopwatch Timer = new Stopwatch();
				while (!StoppingToken.IsCancellationRequested)
				{
					// If the tick immediately event is set, reset it 
					if(TickImmediatelyEvent.IsSet())
					{
						TickImmediatelyEvent = new AsyncEvent();
					}

					// Run the tick method
					Timer.Restart();
					using (IScope Scope = GlobalTracer.Instance.BuildSpan($"{GetType().Name}.{nameof(TickAsync)}").StartActive())
					{
						try
						{
							await TickAsync(StoppingToken);
						}
						catch (Exception Ex)
						{
							if (StoppingToken.IsCancellationRequested)
							{
								break;
							}
							else
							{
								Logger.LogError(Ex, "Unhandled exception in {ServiceName}", GetType().Name);
							}
						}
					}

					// Wait until the next interval has elapsed
					TimeSpan Delay = Interval - Timer.Elapsed;
					if(Delay > TimeSpan.Zero)
					{
						await Task.WhenAny(Task.Delay(Delay), StoppingTask.Task);
					}
				}
			}
		}

		/// <summary>
		/// Abstract tick method to be implemented by derived classes
		/// </summary>
		/// <param name="StoppingToken">Token signalling that the task should terminate</param>
		/// <returns>Async task</returns>
		protected abstract Task TickAsync(CancellationToken StoppingToken);
		
		/// <summary>
		/// Tick the implemented method from above
		/// **Only for use in testing!**
		/// </summary>
		/// <param name="StoppingToken">Optional token signalling that the task should terminate</param>
		/// <returns>Async task</returns>
		public Task TickOnlyForTestingAsync(CancellationToken? StoppingToken = null)
		{
			return TickAsync(StoppingToken ?? new CancellationToken());
		}
	}
}
