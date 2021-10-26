// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Caches a value and asynchronously updates it after a period of time
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class AsyncCachedValue<T>
	{
		class State
		{
			public readonly T Value;
			Stopwatch Timer;
			public Task<State>? Next;

			public TimeSpan Elapsed => Timer.Elapsed;

			public State(T Value)
			{
				this.Value = Value;
				this.Timer = Stopwatch.StartNew();
			}
		}

		/// <summary>
		/// The current state
		/// </summary>
		Task<State>? Current = null;

		/// <summary>
		/// Generator for the new value
		/// </summary>
		Func<Task<T>> Generator;

		/// <summary>
		/// Time at which to start to refresh the value
		/// </summary>
		TimeSpan MinRefreshTime;

		/// <summary>
		/// Time at which to wait for the value to refresh
		/// </summary>
		TimeSpan MaxRefreshTime;

		/// <summary>
		/// Default constructor
		/// </summary>
		public AsyncCachedValue(Func<Task<T>> Generator, TimeSpan RefreshTime)
			: this(Generator, RefreshTime * 0.75, RefreshTime)
		{
		}

		/// <summary>
		/// Default constructor
		/// </summary>
		public AsyncCachedValue(Func<Task<T>> Generator, TimeSpan MinRefreshTime, TimeSpan MaxRefreshTime)
		{
			this.Generator = Generator;
			this.MinRefreshTime = MinRefreshTime;
			this.MaxRefreshTime = MaxRefreshTime;
		}

		/// <summary>
		/// Tries to get the current value
		/// </summary>
		/// <returns>The cached value, if valid</returns>
		public Task<T> GetAsync()
		{
			return GetAsync(MaxRefreshTime);
		}

		Task<State> CreateUpdateTask(ref Task<State>? UpdateTask)
		{
			Task<State>? CurrentTask = UpdateTask;
			while (CurrentTask == null)
			{
				TaskCompletionSource<State> NextTaskSource = new TaskCompletionSource<State>();
				if (Interlocked.CompareExchange(ref UpdateTask, NextTaskSource.Task, null) == null)
				{
					Task.Run(async () => NextTaskSource.SetResult(new State(await Generator())));
					return NextTaskSource.Task;
				}
				CurrentTask = UpdateTask;
			}
			return CurrentTask;
		}

		/// <summary>
		/// Tries to get the current value
		/// </summary>
		/// <returns>The cached value, if valid</returns>
		public async Task<T> GetAsync(TimeSpan MaxAge)
		{
			Task<State> CurrentCopy = CreateUpdateTask(ref Current);

			State CurrentState = await CurrentCopy;
			if (CurrentState.Elapsed > MaxAge)
			{
				CurrentState = await CreateUpdateTask(ref CurrentState.Next);
				return CurrentState.Value;
			}
			if (CurrentState.Elapsed > MinRefreshTime)
			{
				_ = CreateUpdateTask(ref CurrentState.Next);
			}
			if (CurrentState.Next != null && CurrentState.Next.IsCompleted)
			{
				_ = Interlocked.CompareExchange(ref Current, CurrentState.Next, CurrentCopy);
				return CurrentState.Next.Result.Value;
			}

			return CurrentState.Value;
		}
	}
}
