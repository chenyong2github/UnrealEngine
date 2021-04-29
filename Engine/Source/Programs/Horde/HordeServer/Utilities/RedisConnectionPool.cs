// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using StackExchange.Redis;

namespace HordeServer.Utilities
{
	/// <summary>
	/// A connection pool for Redis client
	///
	/// Wraps multiple ConnectionMultiplexers as lazy values and initializes them as needed.
	/// If full, the least loaded connection will be picked.
	/// </summary>
	public class RedisConnectionPool
	{
		private readonly ConcurrentBag<Lazy<ConnectionMultiplexer>> Connections;
		private readonly int DefaultDatabaseIndex;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="PoolSize">Size of the pool (i.e max number of connections)</param>
		/// <param name="RedisConfString">Configuration string for Redis</param>
		/// <param name="DefaultDatabaseIndex">The Redis database index to use. Use -1 for the default one</param>
		public RedisConnectionPool(int PoolSize, string RedisConfString, int DefaultDatabaseIndex = -1)
		{
			this.DefaultDatabaseIndex = DefaultDatabaseIndex;
			Connections = new ConcurrentBag<Lazy<ConnectionMultiplexer>>();
			for (int i = 0; i < PoolSize; i++)
			{
				Connections.Add(new Lazy<ConnectionMultiplexer>(() => ConnectionMultiplexer.Connect(RedisConfString)));
			}
		}

		/// <summary>
		/// Get a connection from the pool
		///
		/// It will pick the least loaded connection or create a new one (if pool size allows)
		/// </summary>
		/// <returns>A Redis database connection</returns>
		public IConnectionMultiplexer GetConnection()
		{
			Lazy<ConnectionMultiplexer> LazyConnection;
			var LazyConnections = Connections.Where(x => x.IsValueCreated);

			if (LazyConnections.Count() == Connections.Count)
			{
				// No more new connections can be created, pick the least loaded one
				LazyConnection = Connections.OrderBy(x => x.Value.GetCounters().TotalOutstanding).First();
			}
			else
			{
				// Create a new connection by picking a not yet initialized lazy value 
				LazyConnection = Connections.First(x => !x.IsValueCreated);
			}

			return LazyConnection.Value;
		}

		/// <summary>
		/// Shortcut to getting a IDatabase
		/// </summary>
		/// <returns>A Redis database</returns>
		public IDatabase GetDatabase()
		{
			return GetConnection().GetDatabase(DefaultDatabaseIndex);
		}

	}
}