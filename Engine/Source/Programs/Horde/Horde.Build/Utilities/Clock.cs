// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace HordeCommon
{
	/// <summary>
	/// Base interface for a scheduled event
	/// </summary>
	public interface ITicker : IDisposable
	{
		/// <summary>
		/// Start the ticker
		/// </summary>
		Task StartAsync();

		/// <summary>
		/// Stop the ticker
		/// </summary>
		Task StopAsync();
	}

	/// <summary>
	/// Interface representing time and scheduling events which is pluggable during testing. In normal use, the Clock implementation below is used. 
	/// </summary>
	public interface IClock
	{
		/// <summary>
		/// Return time expressed as the Coordinated Universal Time (UTC)
		/// </summary>
		/// <returns></returns>
		DateTime UtcNow { get; }

		/// <summary>
		/// Create an event that will trigger after the given time
		/// </summary>
		/// <param name="Interval">Time after which the event will trigger</param>
		/// <param name="TickAsync">Callback for the tick. Returns the time interval until the next tick, or null to cancel the tick.</param>
		/// <param name="Logger">Logger for error messages</param>
		/// <returns>Handle to the event</returns>
		ITicker CreateTicker(TimeSpan Interval, Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync, ILogger Logger);
	}

	/// <summary>
	/// Extension methods for <see cref="IClock"/>
	/// </summary>
	public static class ClockExtensions
	{
		/// <summary>
		/// Create an event that will trigger after the given time
		/// </summary>
		/// <param name="Clock">Clock to schedule the event on</param>
		/// <param name="Interval">Interval for the callback</param>
		/// <param name="TickAsync">Trigger callback</param>
		/// <param name="Logger">Logger for any error messages</param>
		/// <returns>Handle to the event</returns>
		public static ITicker CreateTicker(this IClock Clock, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger)
		{
			Func<CancellationToken, ValueTask<TimeSpan?>> WrappedTrigger = async Token =>
			{
				Stopwatch Timer = Stopwatch.StartNew();
				await TickAsync(Token);
				return Interval - Timer.Elapsed;
			};
			return Clock.CreateTicker(Interval, WrappedTrigger, Logger);
		}
	}

	/// <summary>
	/// Implementation of <see cref="IClock"/> which returns the current time
	/// </summary>
	public class Clock : IClock
	{
		class TickerImpl : ITicker
		{
			[System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "<Pending>")]
			CancellationTokenSource? CancellationSource;
			Func<Task> TickFunc;
			Task? BackgroundTask;

			public TickerImpl(TimeSpan Delay, Func<CancellationToken, ValueTask<TimeSpan?>> TriggerAsync, ILogger Logger)
			{
				TickFunc = () => Run(Delay, TriggerAsync, Logger);
			}

			public async Task StartAsync()
			{
				await StopAsync();
				CancellationSource = new CancellationTokenSource();
				BackgroundTask = Task.Run(TickFunc);
			}

			public async Task StopAsync()
			{
				if (CancellationSource != null)
				{
					CancellationSource.Cancel();
					await BackgroundTask!;
					CancellationSource.Dispose();
					CancellationSource = null;
				}
			}

			public void Dispose()
			{
				StopAsync().Wait();
				if (CancellationSource != null)
				{
					CancellationSource.Dispose();
				}
			}

			public async Task Run(TimeSpan Delay, Func<CancellationToken, ValueTask<TimeSpan?>> TriggerAsync, ILogger Logger)
			{
				while (!CancellationSource!.IsCancellationRequested)
				{
					try
					{
						if (Delay > TimeSpan.Zero)
						{
							await Task.Delay(Delay, CancellationSource.Token);
						}

						TimeSpan? NextDelay = await TriggerAsync(CancellationSource.Token);
						if(NextDelay == null)
						{
							break;
						}

						Delay = NextDelay.Value;
					}
					catch (OperationCanceledException) when (CancellationSource.IsCancellationRequested)
					{
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Exception while executing scheduled event");
						if (Delay < TimeSpan.Zero)
						{
							Delay = TimeSpan.FromSeconds(5.0);
							Logger.LogWarning("Delaying tick for 5 seconds");
						}
					}
				}
			}
		}

		/// <inheritdoc/>
		public DateTime UtcNow => DateTime.UtcNow;

		/// <summary>
		/// Create an event that will trigger after the given time for one agent in the cluster
		/// </summary>
		/// <param name="Delay">Time after which the event will trigger</param>
		/// <param name="TriggerAsync">Callback for the event triggering</param>
		/// <param name="Logger">Logger for error messages</param>
		/// <returns>Handle to the event</returns>
		public ITicker CreateTicker(TimeSpan Delay, Func<CancellationToken, ValueTask<TimeSpan?>> TriggerAsync, ILogger Logger)
		{
			return new TickerImpl(Delay, TriggerAsync, Logger);
		}
	}
	
	/// <summary>
	/// Fake clock that doesn't advance by wall block time
	/// Requires manual ticking to progress. Used in tests.
	/// </summary>
	public class FakeClock : IClock
	{
		class TickerImpl : ITicker
		{
			FakeClock Outer { get; }
			TimeSpan Interval;
			public DateTime? NextTime { get; set; }
			public Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync { get; }

			public TickerImpl(FakeClock Outer, TimeSpan Interval, Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync)
			{
				this.Outer = Outer;
				this.Interval = Interval;
				this.TickAsync = TickAsync;

				lock (Outer.Triggers)
				{
					Outer.Triggers.Add(this);
				}
			}

			public Task StartAsync()
			{
				NextTime = Outer.UtcNow + Interval;
				return Task.CompletedTask;
			}

			public Task StopAsync()
			{
				NextTime = null;
				return Task.CompletedTask;
			}

			public void Dispose()
			{
				DisposeAsync().AsTask().Wait();
			}

			public ValueTask DisposeAsync()
			{
				lock (Outer.Triggers)
				{
					Outer.Triggers.Remove(this);
				}
				return new ValueTask();
			}
		}

		DateTime UtcNowPrivate;
		List<TickerImpl> Triggers = new List<TickerImpl>();

		/// <summary>
		/// Constructor
		/// </summary>
		public FakeClock()
		{
			UtcNowPrivate = DateTime.UtcNow;
		}

		/// <summary>
		/// Advance time by given amount
		/// Useful for letting time progress during tests
		/// </summary>
		/// <param name="Period">Time span to advance</param>
		public async Task AdvanceAsync(TimeSpan Period)
		{
			UtcNowPrivate = UtcNowPrivate.Add(Period);

			for (int Idx = 0; Idx < Triggers.Count; Idx++)
			{
				TickerImpl Trigger = Triggers[Idx];
				while (Trigger.NextTime != null && UtcNowPrivate > Trigger.NextTime)
				{
					TimeSpan? Delay = await Trigger.TickAsync(CancellationToken.None);
					if (Delay == null)
					{
						Triggers.RemoveAt(Idx--);
						break;
					}
					Trigger.NextTime = UtcNowPrivate + Delay.Value;
				}
			}
		}

		/// <inheritdoc/>
		public DateTime UtcNow
		{ 
			get => UtcNowPrivate;
			set => UtcNowPrivate = value.ToUniversalTime(); 
		}

		/// <inheritdoc/>
		public ITicker CreateTicker(TimeSpan Interval, Func<CancellationToken, ValueTask<TimeSpan?>> TriggerAsync, ILogger Logger)
		{
			return new TickerImpl(this, Interval, TriggerAsync);
		}
	}
}