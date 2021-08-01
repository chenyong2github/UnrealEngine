// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Implements a multi-channel message queue using Redis
	/// </summary>
	class RedisMessageQueue<TChannel, TMessage>
	{
		/// <summary>
		/// The Redis database
		/// </summary>
		IDatabase Redis;

		/// <summary>
		/// Prefix for all keys used by the queue
		/// </summary>
		RedisKey KeyPrefix;

		/// <summary>
		/// Key for the index, storing the name of each queue
		/// </summary>
		RedisKey IndexKey;

		/// <summary>
		/// Cache tracking when each channel was last refreshed in the index 
		/// </summary>
		Dictionary<RedisKey, double> ChannelNameToScore = new Dictionary<RedisKey, double>();

		/// <summary>
		/// Time after which entries should be expired
		/// </summary>
		public TimeSpan ExpireTime { get; set; } = TimeSpan.FromSeconds(120);

		/// <summary>
		/// Age at which channel index entries should be updated
		/// </summary>
		TimeSpan IndexRefreshTime => ExpireTime * 0.5;

		/// <summary>
		/// How often to clean up expired entries from the index cache
		/// </summary>
		TimeSpan IndexShrinkTime = TimeSpan.FromSeconds(10.0);

		/// <summary>
		/// Gets the known channel names
		/// </summary>
		public IEnumerable<RedisKey> ChannelNames => ChannelNameToScore.Keys;

		/// <summary>
		/// Last time that the index  time that the ChannelNameToScore was synchronized with the database
		/// </summary>
		DateTime LastShrinkTime = DateTime.MinValue;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis">The Redis database instance</param>
		/// <param name="KeyPrefix">Prefix for keys to use in the database</param>
		public RedisMessageQueue(IDatabase Redis, RedisKey KeyPrefix)
		{
			this.Redis = Redis;
			this.KeyPrefix = KeyPrefix;
			this.IndexKey = KeyPrefix.Append("index");
		}

		/// <summary>
		/// Add an item to a channel
		/// </summary>
		/// <param name="Channel"></param>
		/// <param name="Message">Message to post</param>
		/// <returns></returns>
		public Task PostAsync(TChannel Channel, TMessage Message)
		{
			return PostAsync(Channel, new[] { Message });
		}

		/// <summary>
		/// Add an item to a channel
		/// </summary>
		/// <param name="Channel"></param>
		/// <param name="Messages"></param>
		/// <returns></returns>
		public async Task PostAsync(TChannel Channel, TMessage[] Messages)
		{
			// Periodically remove expired entries from the cached index
			DateTime UtcNow = DateTime.UtcNow;
			if (UtcNow > LastShrinkTime + IndexShrinkTime)
			{
				Shrink();
			}

			// Get the keys and message values
			byte[] RelativeChannelKeyBytes = (byte[])RedisSerializer.Serialize(Channel);
			RedisKey RelativeChannelKey = RelativeChannelKeyBytes;
			RedisKey FullChannelKey = KeyPrefix.Append(RelativeChannelKey);
			SortedSetEntry[] MessageEntries = Array.ConvertAll(Messages, x => new SortedSetEntry(RedisSerializer.Serialize(x), UtcNow.Ticks));

			// Minimum score to avoid updating the index
			double MinScore = (UtcNow - IndexRefreshTime).Ticks;

			// Refresh this queue time if necessary
			double Score;
			if (ChannelNameToScore.TryGetValue(RelativeChannelKey, out Score) && Score > MinScore)
			{
				await Redis.SortedSetAddAsync(FullChannelKey, MessageEntries, CommandFlags.None);
			}
			else
			{
				IBatch Batch = Redis.CreateBatch();

				List<Task> Tasks = new List<Task>();
				Tasks.Add(Batch.SortedSetAddAsync(IndexKey, RelativeChannelKeyBytes, UtcNow.Ticks, CommandFlags.None));
				Tasks.Add(Batch.SortedSetAddAsync(FullChannelKey, MessageEntries, CommandFlags.None));

				Batch.Execute();

				await Task.WhenAll(Tasks);

				lock (ChannelNameToScore)
				{
					ChannelNameToScore[RelativeChannelKey] = UtcNow.Ticks;
				}
			}
		}

		/// <summary>
		/// Receives a set of items on a channel
		/// </summary>
		/// <param name="Channel"></param>
		/// <param name="MaxItems"></param>
		/// <returns></returns>
		public async Task<List<TMessage>> DequeueAsync(TChannel Channel, int MaxItems)
		{
			RedisKey ChannelKey = KeyPrefix.Append(RedisSerializer.Serialize(Channel).AsKey());

			double MinScore = (DateTime.UtcNow - ExpireTime).Ticks;

			List<TMessage> Messages = new List<TMessage>();
			while (Messages.Count < MaxItems)
			{
				int Count = MaxItems - Messages.Count;

				SortedSetEntry[] Entries = await Redis.SortedSetPopAsync(ChannelKey, Count);
				Messages.AddRange(Entries.Where(x => x.Score >= MinScore).Select(x => RedisSerializer.Deserialize<TMessage>(x.Element)));

				if (Entries.Length < Count)
				{
					break;
				}
			}
			return Messages;
		}

		/// <summary>
		/// Updates cached values used by the queue
		/// </summary>
		/// <returns></returns>
		public void Shrink()
		{
			DateTime UtcNow = DateTime.UtcNow;
			lock (ChannelNameToScore)
			{
				double MinScore = (UtcNow - ExpireTime).Ticks;

				RedisKey[] RemoveKeys = ChannelNameToScore.Where(x => x.Value < MinScore).Select(x => x.Key).ToArray();
				foreach (RedisKey RemoveKey in RemoveKeys)
				{
					ChannelNameToScore.Remove(RemoveKey);
				}
			}
			LastShrinkTime = UtcNow;
		}

		/// <summary>
		/// Removes items which have expired from the queue
		/// </summary>
		/// <returns></returns>
		public async Task RemovedExpiredAsync()
		{
			double MinScore = (DateTime.UtcNow - ExpireTime).Ticks;

			RedisValue[] Values = await Redis.SortedSetRangeByScoreAsync(IndexKey, Double.NegativeInfinity, MinScore);
			if (Values.Length > 0)
			{
				foreach (RedisValue Value in Values)
				{
					RedisKey QueueKey = KeyPrefix.Append(Value.AsKey());
					await Redis.SortedSetRemoveRangeByScoreAsync(QueueKey, Double.NegativeInfinity, MinScore);
				}
				await Redis.SortedSetRemoveRangeByScoreAsync(IndexKey, Double.NegativeInfinity, MinScore);
			}
		}
	}
}
