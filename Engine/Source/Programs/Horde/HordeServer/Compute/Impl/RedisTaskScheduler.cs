// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Redis;
using EpicGames.Serialization;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using StackExchange.Redis;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace HordeServer.Compute.Impl
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Describes an entry for a compute queue.
	/// </summary>
	/// <typeparam name="T">The type describing the work to be performed. Must be serializable to a RedisValue via RedisSerializer.</typeparam>
	class TaskSchedulerEntry<T>
	{
		/// <summary>
		/// The task description, encoded as a RedisValue
		/// </summary>
		public T Item { get; }

		/// <summary>
		/// Hash of the <see cref="Requirements"/> object in CAS for the agent to execute the task
		/// </summary>
		public IoHash RequirementsHash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Item">The task item</param>
		/// <param name="RequirementsHash">Requirements for executing the task</param>
		public TaskSchedulerEntry(T Item, IoHash RequirementsHash)
		{
			this.Item = Item;
			this.RequirementsHash = RequirementsHash;
		}
	}

	/// <summary>
	/// Interface for a queue of compute operations, each of which may have different requirements for the executing machines.
	/// </summary>
	interface ITaskScheduler<T>
	{
		/// <summary>
		/// Adds a task to the queue
		/// </summary>
		/// <param name="Item">The item to add</param>
		/// <param name="RequirementsHash">Hash of a <see cref="Requirements"/> object stored in the CAS which describes the agent to execute the task</param>
		Task EnqueueAsync(T Item, IoHash RequirementsHash);

		/// <summary>
		/// Inserts a previously dequeued task back at the front of the queue
		/// </summary>
		/// <param name="Item">The item to add</param>
		/// <param name="RequirementsHash">Hash of a <see cref="Requirements"/> object stored in the CAS which describes the agent to execute the task</param>
		Task RequeueAsync(T Item, IoHash RequirementsHash);

		/// <summary>
		/// Dequeues a task that the given agent can execute
		/// </summary>
		/// <param name="Agent">The agent to execute the task</param>
		/// <param name="Token">Cancellation token for the operation. Will return a null entry rather than throwing an exception.</param>
		/// <returns>Information about the task to be executed</returns>
		Task<TaskSchedulerEntry<T>?> DequeueAsync(IAgent Agent, CancellationToken Token = default);
	}

	/// <summary>
	/// Implementation of <see cref="ITaskScheduler{T}"/> using Redis for storage
	/// </summary>
	/// <typeparam name="T">The task definition type</typeparam>
	class RedisTaskScheduler<T> : ITaskScheduler<T>, IDisposable where T : class
	{
		class Listener
		{
			public IAgent Agent { get; }
			public TaskCompletionSource<TaskSchedulerEntry<T>?> CompletionSource { get; }

			public Listener(IAgent Agent)
			{
				this.Agent = Agent;
				this.CompletionSource = new TaskCompletionSource<TaskSchedulerEntry<T>?>();
			}
		}

		IDatabase Redis;
		IObjectCollection ObjectCollection;
		RedisKey BaseKey;
		NamespaceId NamespaceId;
		RedisSet<IoHash> QueueIndex;
		RedisHash<IoHash, DateTime> ActiveQueues; // Queues which are actively being dequeued from
		ReadOnlyHashSet<IoHash> LocalActiveQueues = new HashSet<IoHash>();
		Stopwatch ResetActiveQueuesTimer = Stopwatch.StartNew();

		List<Listener> Listeners = new List<Listener>();
		RedisChannel<IoHash> NewQueueChannel;
		Task QueueUpdateTask;
		CancellationTokenSource CancellationSource = new CancellationTokenSource();
		IMemoryCache RequirementsCache;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis">The Redis database instance</param>
		/// <param name="ObjectCollection">Interface to the CAS object store</param>
		/// <param name="BaseKey">Base key for all keys used by this scheduler</param>
		/// <param name="NamespaceId">The namespace identifier</param>
		public RedisTaskScheduler(IDatabase Redis, IObjectCollection ObjectCollection, NamespaceId NamespaceId, RedisKey BaseKey)
		{
			this.Redis = Redis;
			this.ObjectCollection = ObjectCollection;
			this.NamespaceId = NamespaceId;
			this.BaseKey = BaseKey;
			this.QueueIndex = new RedisSet<IoHash>(BaseKey.Append("index"));
			this.ActiveQueues = new RedisHash<IoHash, DateTime>(BaseKey.Append("active"));
			this.NewQueueChannel = new RedisChannel<IoHash>(BaseKey.Append("new_queues").ToString());
			this.RequirementsCache = new MemoryCache(new MemoryCacheOptions());

			QueueUpdateTask = Task.Run(() => UpdateQueuesAsync(CancellationSource.Token));
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			if (!QueueUpdateTask.IsCompleted)
			{
				CancellationSource.Cancel();
				await QueueUpdateTask;
			}
			RequirementsCache.Dispose();
			CancellationSource.Dispose();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			DisposeAsync().AsTask().Wait();
		}

		/// <summary>
		/// Gets the key for a list of tasks for a particular queue
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		RedisKey GetQueueKey(IoHash RequirementsHash)
		{
			return BaseKey.Append(RedisSerializer.Serialize(RequirementsHash).AsKey());
		}

		/// <summary>
		/// Gets the key for a list of tasks for a particular queue
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		RedisList<T> GetQueue(IoHash RequirementsHash)
		{
			return new RedisList<T>(GetQueueKey(RequirementsHash));
		}

		/// <summary>
		/// Adds a queue to the index, atomically checking that the queue exists
		/// </summary>
		/// <param name="RequirementsHash">Hash of the queue requirements objectadd</param>
		async ValueTask AddQueueToIndexAsync(IoHash RequirementsHash)
		{
			ITransaction Transaction = Redis.CreateTransaction();
			Transaction.AddCondition(Condition.KeyExists(GetQueueKey(RequirementsHash)));
			_ = Transaction.SetAddAsync(QueueIndex, RequirementsHash);
			await Transaction.ExecuteAsync();

			await Redis.PublishAsync(NewQueueChannel, RequirementsHash);
		}

		/// <summary>
		/// Removes a queue from the index, atomically checking that it is empty
		/// </summary>
		/// <param name="RequirementsHash">Hash of the queue requirements objectadd</param>
		async ValueTask RemoveQueueFromIndexAsync(IoHash RequirementsHash)
		{
			ITransaction Transaction = Redis.CreateTransaction();
			Transaction.AddCondition(Condition.KeyNotExists(GetQueueKey(RequirementsHash)));
			_ = Transaction.SetRemoveAsync(QueueIndex, RequirementsHash);
			await Transaction.ExecuteAsync(CommandFlags.FireAndForget);
		}

		/// <summary>
		/// Adds an item to a particular queue, creating and adding that queue to the index if necessary
		/// </summary>
		/// <param name="Item">The item to be added</param>
		/// <param name="RequirementsHash">Requirements for the task</param>
		public async Task EnqueueAsync(T Item, IoHash RequirementsHash)
		{
			RedisList<T> List = GetQueue(RequirementsHash);

			long NewLength = await Redis.ListRightPushAsync(List, Item);
			if (NewLength == 1)
			{
				await AddQueueToIndexAsync(RequirementsHash);
			}
		}

		/// <summary>
		/// Adds an item to the front of a particular queue, creating and adding that queue to the index if necessary
		/// </summary>
		/// <param name="Item">The item to be added</param>
		/// <param name="RequirementsHash">Requirements for the task</param>
		public async Task RequeueAsync(T Item, IoHash RequirementsHash)
		{
			RedisList<T> Queue = GetQueue(RequirementsHash);

			long NewLength = await Redis.ListLeftPushAsync(Queue, Item);
			if (NewLength == 1)
			{
				await AddQueueToIndexAsync(RequirementsHash);
			}
		}

		/// <summary>
		/// Dequeues an item for execution by the given agent
		/// </summary>
		/// <param name="Agent">The agent to dequeue an item for</param>
		/// <param name="Token">Cancellation token for waiting for an item</param>
		/// <returns>The dequeued item, or null if no item is available</returns>
		public async Task<TaskSchedulerEntry<T>?> DequeueAsync(IAgent Agent, CancellationToken Token = default)
		{
			// Compare against all the list of cached queues to see if we can dequeue something from any of them
			Listener? Listener = null;
			try
			{
				IoHash[] Queues = await Redis.SetMembersAsync(QueueIndex);
				while (!Token.IsCancellationRequested)
				{
					// Try to dequeue an item from the list
					TaskSchedulerEntry<T>? Entry = await TryAssignToLocalAgentAsync(Queues, Agent);
					if (Entry != null)
					{
						return Entry;
					}

					// Create and register a listener for this waiter 
					if (Listener == null)
					{
						Listener = new Listener(Agent);
						lock (Listeners)
						{
							Listeners.Add(Listener);
						}
					}
					else
					{
						using (IDisposable Registration = Token.Register(() => Listener.CompletionSource.TrySetResult(null)))
						{
							return await Listener.CompletionSource.Task;
						}
					}
				}
			}
			finally
			{
				if (Listener != null)
				{
					lock (Listeners)
					{
						Listeners.Remove(Listener);
					}
				}
			}
			return null;
		}

		async Task<TaskSchedulerEntry<T>?> TryAssignToLocalAgentAsync(IoHash[] Queues, IAgent Agent)
		{
			foreach (IoHash RequirementsHash in Queues)
			{
				Requirements? Requirements = await GetRequirementsAsync(RequirementsHash);
				if (Requirements != null && CheckRequirements(Agent, Requirements))
				{
					T? Item = await DequeueAsync(RequirementsHash);
					if (Item != null)
					{
						return new TaskSchedulerEntry<T>(Item, RequirementsHash);
					}
				}
			}
			return null;
		}

		/// <summary>
		/// Dequeues the item at the front of a queue
		/// </summary>
		/// <param name="RequirementsHash">Key of the queue to remove from</param>
		/// <returns>The dequeued item, or null if the queue is empty</returns>
		public async Task<T?> DequeueAsync(IoHash RequirementsHash)
		{
			await AddActiveQueue(RequirementsHash);

			T? Item = await Redis.ListLeftPopAsync(GetQueue(RequirementsHash));
			if (Item == null)
			{
				await RemoveQueueFromIndexAsync(RequirementsHash);
			}
			return Item;
		}

		/// <summary>
		/// Marks a queue key as being actively monitored, preventing it being returned by <see cref="GetInactiveQueuesAsync"/>
		/// </summary>
		/// <param name="Key">The queue key</param>
		/// <returns></returns>
		async ValueTask AddActiveQueue(IoHash Key)
		{
			// Periodically clear out the set of active keys
			TimeSpan ResetTime = TimeSpan.FromSeconds(10.0);
			if (ResetActiveQueuesTimer.Elapsed > ResetTime)
			{
				lock (ResetActiveQueuesTimer)
				{
					if (ResetActiveQueuesTimer.Elapsed > ResetTime)
					{
						LocalActiveQueues = new HashSet<IoHash>();
						ResetActiveQueuesTimer.Reset();
					}
				}
			}

			// Check if the set of active keys already contains the key we're adding. In order to optimize the 
			// common case under heavy load where the key is in the set, updating it creates a full copy of it. Any
			// readers can thus access it without the need for any locking.
			ReadOnlyHashSet<IoHash> LocalActiveKeysCopy = LocalActiveQueues;
			if (!LocalActiveKeysCopy.Contains(Key))
			{
				for (; ; )
				{
					HashSet<IoHash> NewLocalActiveKeys = new HashSet<IoHash>(LocalActiveKeysCopy);
					if (!NewLocalActiveKeys.Add(Key))
					{
						break;
					}
					if (Interlocked.CompareExchange(ref LocalActiveQueues, NewLocalActiveKeys, LocalActiveKeysCopy) == LocalActiveKeysCopy)
					{
						await Redis.HashSetAsync(ActiveQueues, Key, DateTime.UtcNow);
						break;
					}
				}
			}
		}

		/// <summary>
		/// Find any inactive keys
		/// </summary>
		/// <returns></returns>
		public async Task<List<IoHash>> GetInactiveQueuesAsync()
		{
			HashSet<IoHash> Keys = new HashSet<IoHash>(await Redis.SetMembersAsync(QueueIndex));
			HashSet<IoHash> InvalidKeys = new HashSet<IoHash>();

			DateTime MinTime = DateTime.UtcNow - TimeSpan.FromMinutes(1.0);

			HashEntry<IoHash, DateTime>[] Entries = await Redis.HashGetAllAsync(ActiveQueues);
			foreach (HashEntry<IoHash, DateTime> Entry in Entries)
			{
				if (Entry.Value < MinTime)
				{
					InvalidKeys.Add(Entry.Name);
				}
				else
				{
					Keys.Remove(Entry.Name);
				}
			}

			if (InvalidKeys.Count > 0)
			{
				await Redis.HashDeleteAsync(ActiveQueues, InvalidKeys.ToArray());
			}

			return Keys.ToList();
		}

		async Task UpdateQueuesAsync(CancellationToken CancellationToken)
		{
			Channel<IoHash> NewQueues = Channel.CreateUnbounded<IoHash>();

			ISubscriber Subscriber = Redis.Multiplexer.GetSubscriber();
			await using var _ = await Subscriber.SubscribeAsync(NewQueueChannel, (_, v) => NewQueues.Writer.TryWrite(v));

			while (await NewQueues.Reader.WaitToReadAsync(CancellationToken))
			{
				HashSet<IoHash> NewQueueKeys = new HashSet<IoHash>();
				while (NewQueues.Reader.TryRead(out IoHash Key))
				{
					NewQueueKeys.Add(Key);
				}
				foreach (IoHash NewQueueKey in NewQueueKeys)
				{
					await TryDispatchToNewQueueAsync(NewQueueKey);
				}
			}
		}

		async Task<bool> TryDispatchToNewQueueAsync(IoHash RequirementsHash)
		{
			RedisList<T> Queue = GetQueue(RequirementsHash);

			// Get the requirements for this queue
			Requirements? Requirements = await GetRequirementsAsync(RequirementsHash);
			if (Requirements == null)
			{
				throw new Exception($"Unable to find requirements with hash {RequirementsHash}");
			}

			// Find a local listener that can execute the work
			TaskSchedulerEntry<T>? Entry = null;
			try
			{
				for (; ; )
				{
					// Look for a listener that can execute the task
					Listener? Listener;
					lock (Listeners)
					{
						Listener = Listeners.FirstOrDefault(x => !x.CompletionSource.Task.IsCompleted && CheckRequirements(x.Agent, Requirements));
					}
					if (Listener == null)
					{
						return false;
					}

					// Pop an entry from the queue
					if (Entry == null)
					{
						T? TaskData = await DequeueAsync(RequirementsHash);
						if (TaskData == null)
						{
							return false;
						}
						Entry = new TaskSchedulerEntry<T>(TaskData, RequirementsHash);
					}

					// Assign it to the listener
					if (Listener.CompletionSource.TrySetResult(Entry))
					{
						Entry = null;
					}
				}
			}
			finally
			{
				if (Entry != null)
				{
					await RequeueAsync(Entry.Item, Entry.RequirementsHash);
				}
			}
		}

		static bool CheckRequirements(IAgent Agent, Requirements Requirements)
		{
			_ = Agent;
			_ = Requirements;
			// TODO: check requirements
			// TODO: cache requirements
			return true;
		}

		/// <summary>
		/// Gets the requirements object from the CAS
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		async Task<Requirements?> GetRequirementsAsync(IoHash RequirementsHash)
		{
			Requirements? Requirements;
			if (!RequirementsCache.TryGetValue(RequirementsHash, out Requirements))
			{
				Requirements = await ObjectCollection.GetAsync<Requirements>(NamespaceId, RequirementsHash);
				using (ICacheEntry Entry = RequirementsCache.CreateEntry(RequirementsHash))
				{
					Entry.SetSlidingExpiration(TimeSpan.FromMinutes(10.0));
					Entry.SetValue(Requirements);
				}
			}
			return Requirements;
		}
	}
}
