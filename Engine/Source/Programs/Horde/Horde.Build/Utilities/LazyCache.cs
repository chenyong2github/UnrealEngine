// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Options for <see cref="LazyCache{TKey, TValue}"/>
	/// </summary>
	public class LazyCacheOptions
	{
		/// <summary>
		/// Time after which a value will asynchronously be updated
		/// </summary>
		public TimeSpan? RefreshTime { get; set; } = TimeSpan.FromMinutes(1.0);

		/// <summary>
		/// Maximum age of any returned value. This will prevent a cached value being returned.
		/// </summary>
		public TimeSpan? MaxAge { get; set; } = TimeSpan.FromMinutes(2.0);
	}

	/// <summary>
	/// Implements a cache which starts an asynchronous update of a value after a period of time.
	/// </summary>
	/// <typeparam name="TKey">Key for the cache</typeparam>
	/// <typeparam name="TValue">Value for the cache</typeparam>
	public sealed class LazyCache<TKey, TValue> : IDisposable where TKey : notnull
	{
		class Item
		{
			public Task<TValue>? CurrentTask;
			public Stopwatch Timer = Stopwatch.StartNew();
			public Task<TValue>? UpdateTask;
		}

		ConcurrentDictionary<TKey, Item> Dictionary = new ConcurrentDictionary<TKey, Item>();
		Func<TKey, Task<TValue>> GetValueAsync;
		LazyCacheOptions Options;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="GetValueAsync">Function used to get a value</param>
		/// <param name="Options"></param>
		public LazyCache(Func<TKey, Task<TValue>> GetValueAsync, LazyCacheOptions Options)
		{
			this.GetValueAsync = GetValueAsync;
			this.Options = Options;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			foreach (Item Item in Dictionary.Values)
			{
				Item.CurrentTask?.Wait();
				Item.UpdateTask?.Wait();
			}
		}

		/// <summary>
		/// Gets the value associated with a key
		/// </summary>
		/// <param name="Key">The key to query</param>
		/// <param name="MaxAge">Maximum age for values to return</param>
		/// <returns></returns>
		public Task<TValue> GetAsync(TKey Key, TimeSpan? MaxAge = null)
		{
			Item Item = Dictionary.GetOrAdd(Key, Key => new Item());

			// Create the task to get the current value
			Task<TValue> CurrentTask = InterlockedCreateTask(ref Item.CurrentTask, () => GetValueAsync(Key));

			// If an update has completed, swap it out
			Task<TValue>? UpdateTask = Item.UpdateTask;
			if (UpdateTask != null && UpdateTask.IsCompleted)
			{
				Interlocked.CompareExchange(ref Item.CurrentTask, UpdateTask, CurrentTask);
				Interlocked.CompareExchange(ref Item.UpdateTask, null, UpdateTask);
				Item.Timer.Restart();
			}

			// Check if we need to update the value
			TimeSpan Age = Item.Timer.Elapsed;
			if (MaxAge != null && Age > MaxAge.Value)
			{
				return InterlockedCreateTask(ref Item.UpdateTask, () => GetValueAsync(Key));
			}
			if (Age > Options.RefreshTime)
			{
				InterlockedCreateTask(ref Item.UpdateTask, () => GetValueAsync(Key));
			}

			return CurrentTask;
		}

		/// <summary>
		/// Creates a task, guaranteeing that only one task will be assigned to the given slot. Creates a cold task and only starts it once the variable is set.
		/// </summary>
		/// <param name="Value"></param>
		/// <param name="CreateTask"></param>
		/// <returns></returns>
		static Task<TValue> InterlockedCreateTask(ref Task<TValue>? Value, Func<Task<TValue>> CreateTask)
		{
			Task<TValue>? CurrentTask = Value;
			while (CurrentTask == null)
			{
				Task<Task<TValue>> NewTask = new Task<Task<TValue>>(CreateTask);
				if (Interlocked.CompareExchange(ref Value, NewTask.Unwrap(), null) == null)
				{
					NewTask.Start();
				}
				CurrentTask = Value;
			}
			return CurrentTask;
		}
	}
}
