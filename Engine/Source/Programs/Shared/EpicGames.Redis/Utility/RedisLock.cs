// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Redis.Utility
{
	/// <summary>
	/// Implements a named single-entry lock which expires after a period of time if the process terminates.
	/// </summary>
	public class RedisLock : IAsyncDisposable, IDisposable
	{
		IDatabase Database;
		RedisKey Key;
		CancellationTokenSource? CancellationSource;
		Task? BackgroundTask;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Database"></param>
		/// <param name="Key"></param>
		public RedisLock(IDatabase Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			DisposeAsync().AsTask().Wait();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			if (BackgroundTask != null)
			{
				CancellationSource!.Cancel();
				await BackgroundTask;
				CancellationSource.Dispose();
				BackgroundTask = null;
			}
		}

		/// <summary>
		/// Attempts to acquire the lock for the given period of time. The lock will be renewed once half of this interval has elapsed.
		/// </summary>
		/// <param name="Duration">Time after which the lock expires</param>
		/// <returns>True if the lock was acquired, false if another service already has it</returns>
		public async ValueTask<bool> AcquireAsync(TimeSpan Duration)
		{
			if (await Database.StringSetAsync(Key, RedisValue.EmptyString, Duration, When.NotExists))
			{
				CancellationSource = new CancellationTokenSource();
				BackgroundTask = Task.Run(() => RenewAsync(Duration, CancellationSource.Token));
				return true;
			}
			return false;
		}

		/// <summary>
		/// Background task which renews the lock while the service is running
		/// </summary>
		/// <param name="Duration"></param>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async Task RenewAsync(TimeSpan Duration, CancellationToken CancellationToken)
		{
			for (; ; )
			{
				await Task.Delay(Duration / 2, CancellationToken).ContinueWith(x => { }); // Do not throw
				if (CancellationToken.IsCancellationRequested)
				{
					await Database.StringSetAsync(Key, RedisValue.Null);
					break;
				}
				if (!await Database.StringSetAsync(Key, RedisValue.EmptyString, Duration, When.Exists))
				{
					break;
				}
			}
		}
	}

	/// <summary>
	/// Implements a named single-entry lock which expires after a period of time if the process terminates.
	/// </summary>
	/// <typeparam name="T">Type of the value identifying the lock uniqueness</typeparam>
	public class RedisLock<T> : RedisLock
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Database"></param>
		/// <param name="BaseKey"></param>
		/// <param name="Value"></param>
		public RedisLock(IDatabase Database, RedisKey BaseKey, T Value)
			: base(Database, BaseKey.Append(RedisSerializer.Serialize<T>(Value).AsKey()))
		{
		}
	}
}
