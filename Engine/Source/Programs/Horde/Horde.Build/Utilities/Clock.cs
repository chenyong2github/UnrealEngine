// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Redis.Utility;
using HordeServer.Services;
using Microsoft.Extensions.Logging;
using StackExchange.Redis;
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
	/// Placeholder interface for ITicker
	/// </summary>
	public sealed class NullTicker : ITicker
	{
		/// <inheritdoc/>
		public void Dispose() { }

		/// <inheritdoc/>
		public Task StartAsync() => Task.CompletedTask;

		/// <inheritdoc/>
		public Task StopAsync() => Task.CompletedTask;
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
		ITicker AddTicker(TimeSpan Interval, Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync, ILogger Logger);

		/// <summary>
		/// Create a ticker shared between all server pods
		/// </summary>
		/// <param name="Name">Name of the event</param>
		/// <param name="Interval">Time after which the event will trigger</param>
		/// <param name="TickAsync">Callback for the tick. Returns the time interval until the next tick, or null to cancel the tick.</param>
		/// <param name="Logger">Logger for error messages</param>
		/// <returns>New ticker instance</returns>
		ITicker AddSharedTicker(string Name, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger);
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
		public static ITicker AddTicker(this IClock Clock, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger)
		{
			Func<CancellationToken, ValueTask<TimeSpan?>> WrappedTrigger = async Token =>
			{
				Stopwatch Timer = Stopwatch.StartNew();
				await TickAsync(Token);
				return Interval - Timer.Elapsed;
			};
			return Clock.AddTicker(Interval, WrappedTrigger, Logger);
		}

		/// <summary>
		/// Create a ticker shared between all server pods
		/// </summary>
		/// <param name="Clock">Clock to schedule the event on</param>
		/// <param name="Interval">Time after which the event will trigger</param>
		/// <param name="TickAsync">Callback for the tick. Returns the time interval until the next tick, or null to cancel the tick.</param>
		/// <param name="Logger">Logger for error messages</param>
		/// <returns>New ticker instance</returns>
		public static ITicker AddSharedTicker<T>(this IClock Clock, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger) => Clock.AddSharedTicker(typeof(T).Name, Interval, TickAsync, Logger);
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

		RedisService Redis;

		/// <inheritdoc/>
		public DateTime UtcNow => DateTime.UtcNow;

		/// <summary>
		/// Constructor
		/// </summary>
		public Clock(RedisService Redis)
		{
			this.Redis = Redis;
		}

		/// <inheritdoc/>
		public ITicker AddTicker(TimeSpan Delay, Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync, ILogger Logger)
		{
			return new TickerImpl(Delay, TickAsync, Logger);
		}

		/// <inheritdoc/>
		public ITicker AddSharedTicker(string Name, TimeSpan Delay, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger)
		{
			RedisKey Key = new RedisKey($"tick/{Name}");
			return ClockExtensions.AddTicker(this, Delay / 4, Token => TriggerSharedAsync(Key, Delay, TickAsync, Token), Logger);
		}

		async ValueTask TriggerSharedAsync(RedisKey Key, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, CancellationToken CancellationToken)
		{
			using (RedisLock Lock = new RedisLock(Redis.Database, Key))
			{
				if (await Lock.AcquireAsync(Interval, false))
				{
					await TickAsync(CancellationToken);
				}
			}
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
		public ITicker AddTicker(TimeSpan Interval, Func<CancellationToken, ValueTask<TimeSpan?>> TickAsync, ILogger Logger)
		{
			return new TickerImpl(this, Interval, TickAsync);
		}

		/// <inheritdoc/>
		public ITicker AddSharedTicker(string Name, TimeSpan Interval, Func<CancellationToken, ValueTask> TickAsync, ILogger Logger)
		{
			return AddTicker(Interval, async Token => { await TickAsync(Token); return Interval; }, Logger);
		}
	}
}