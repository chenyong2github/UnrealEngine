// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Redis;
using HordeServer.Compute.Impl;
using Microsoft.Extensions.Logging;
using StackExchange.Redis;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Compute.Impl
{
	/// <summary>
	/// Interface for a distributed message queue
	/// </summary>
	/// <typeparam name="T">The message type</typeparam>
	interface IMessageQueue<T> where T : class
	{
		/// <summary>
		/// Adds messages to the queue
		/// </summary>
		/// <param name="ChannelId">The channel to post to</param>
		/// <param name="Message">Message to post</param>
		Task PostAsync(string ChannelId, T Message);

		/// <summary>
		/// Waits for a message to be available on the given channel
		/// </summary>
		/// <param name="ChannelId">The channel to read from</param>
		/// <returns>True if a message was available, false otherwise</returns>
		Task<List<T>> ReadMessagesAsync(string ChannelId);

		/// <summary>
		/// Waits for a message to be available on the given channel
		/// </summary>
		/// <param name="ChannelId">The channel to read from</param>
		/// <param name="CancellationToken">May be signalled to stop the wait, without throwing an exception</param>
		/// <returns>True if a message was available, false otherwise</returns>
		Task<List<T>> WaitForMessagesAsync(string ChannelId, CancellationToken CancellationToken);
	}

	/// <summary>
	/// Implementation of a message queue using Redis
	/// </summary>
	/// <typeparam name="T">The message type. Must be serailizable by RedisSerializer.</typeparam>
	class RedisMessageQueue<T> : IMessageQueue<T>, IDisposable where T : class
	{
		IDatabase Redis;
		RedisKey KeyPrefix;
		RedisChannel UpdateChannel;
		Dictionary<string, TaskCompletionSource<bool>> ChannelWakeEvents = new Dictionary<string, TaskCompletionSource<bool>>();

		// Time after which entries should be removed
		TimeSpan ExpireTime { get; set; } = TimeSpan.FromSeconds(30);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis">The Redis database instance</param>
		/// <param name="KeyPrefix">Prefix for keys to use in the database</param>
		public RedisMessageQueue(IDatabase Redis, RedisKey KeyPrefix)
		{
			this.Redis = Redis;
			this.KeyPrefix = KeyPrefix;
			this.UpdateChannel = KeyPrefix.Append("updates").ToString();

			Redis.Multiplexer.GetSubscriber().Subscribe(UpdateChannel, OnChannelUpdate);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Redis.Multiplexer.GetSubscriber().Unsubscribe(UpdateChannel, OnChannelUpdate);
		}

		/// <summary>
		/// Callback for a message being posted to a channel
		/// </summary>
		/// <param name="Channel"></param>
		/// <param name="Value"></param>
		void OnChannelUpdate(RedisChannel Channel, RedisValue Value)
		{
			TaskCompletionSource<bool>? CompletionSource;
			lock (ChannelWakeEvents)
			{
				ChannelWakeEvents.TryGetValue(Value.ToString(), out CompletionSource);
			}
			if (CompletionSource != null)
			{
				CompletionSource.TrySetResult(true);
			}
		}

		/// <summary>
		/// Gets the set of messages for a channel
		/// </summary>
		/// <param name="ChannelId"></param>
		/// <returns></returns>
		RedisList<T> GetChannel(string ChannelId)
		{
			return new RedisList<T>(Redis, KeyPrefix.Append(ChannelId));
		}

		/// <inheritdoc/>
		public async Task PostAsync(string ChannelId, T Message)
		{
			RedisList<T> Channel = GetChannel(ChannelId);

			long Length = await Channel.RightPushAsync(Message);
			if (Length == 1)
			{
				await Redis.PublishAsync(UpdateChannel, ChannelId, CommandFlags.FireAndForget);
			}
			await Redis.KeyExpireAsync(Channel.Key, ExpireTime, CommandFlags.FireAndForget);
		}

		static async Task<bool> ReadMessagesAsync(RedisList<T> List, List<T> Messages)
		{
			T? Message = await List.LeftPopAsync();
			while(Message != null)
			{
				Messages.Add(Message);
				Message = await List.LeftPopAsync();
			}
			return Messages.Count > 0;
		}

		/// <inheritdoc/>
		public async Task<List<T>> ReadMessagesAsync(string ChannelId)
		{
			List<T> Messages = new List<T>();
			await ReadMessagesAsync(GetChannel(ChannelId), Messages);
			return Messages;
		}

		/// <inheritdoc/>
		public async Task<List<T>> WaitForMessagesAsync(string ChannelId, CancellationToken CancellationToken)
		{
			List<T> Messages = new List<T>();

			RedisList<T> Channel = GetChannel(ChannelId);
			while (!CancellationToken.IsCancellationRequested && !await ReadMessagesAsync(Channel, Messages))
			{
				// Register for notifications on this channel
				TaskCompletionSource<bool>? CompletionSource;
				lock (ChannelWakeEvents)
				{
					if (!ChannelWakeEvents.TryGetValue(ChannelId, out CompletionSource))
					{
						CompletionSource = new TaskCompletionSource<bool>();
						ChannelWakeEvents.Add(ChannelId, CompletionSource);
					}
				}

				try
				{
					// Read the current queue state again, in case it was modified since we registered.
					if(await ReadMessagesAsync(Channel, Messages))
					{
						break;
					}

					// Wait for messages to be available
					using (CancellationToken.Register(() => CompletionSource.TrySetResult(false)))
					{
						await CompletionSource.Task;
					}
				}
				finally
				{
					lock (ChannelWakeEvents)
					{
						ChannelWakeEvents.Remove(ChannelId);
					}
				}
			}

			return Messages;
		}
	}
}
