// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Driver;
using StackExchange.Redis;

namespace HordeServerTests
{
    // Stub for fulfilling IOptionsMonitor interface during testing
    public class TestOptionsMonitor<T> : IOptionsMonitor<T>, IDisposable
        where T : class, new()
    {
        public TestOptionsMonitor(T CurrentValue)
        {
            this.CurrentValue = CurrentValue;
        }

        public T Get(string Name)
        {
            return CurrentValue;
        }

        public IDisposable OnChange(Action<T, string> listener)
        {
            return this;
        }

        public T CurrentValue { get; }
        
        public void Dispose()
        {
	        // Dummy stub to satisfy return value of OnChange 
        }
    }

	public class MongoDbInstance : IDisposable
	{
		public string DatabaseName { get; }
		public string ConnectionString { get; }
		MongoClient Client { get; }

		private static object LockObject = new object();
		private static MongoDbRunnerLocal? _mongoDbRunner;
		private static int NextDatabaseIndex = 1;
		public const string MongoDbDatabaseNamePrefix = "HordeServerTest_";

		public MongoDbInstance()
		{
			int DatabaseIndex;
			lock (LockObject)
			{
				if (_mongoDbRunner == null)
				{
					// One-time setup per test run to avoid overhead of starting the external MongoDB process
					Startup.ConfigureMongoDbClient();
					_mongoDbRunner = new MongoDbRunnerLocal();
					_mongoDbRunner.Start();

					// Drop all the previous databases
					MongoClientSettings MongoSettings = MongoClientSettings.FromConnectionString(_mongoDbRunner.GetConnectionString());
					MongoClient Client = new MongoClient(MongoSettings);

					List<string> DropDatabaseNames = Client.ListDatabaseNames().ToList();
					foreach (string DropDatabaseName in DropDatabaseNames)
					{
						if (DropDatabaseName.StartsWith(MongoDbDatabaseNamePrefix, StringComparison.Ordinal))
						{
							Client.DropDatabase(DropDatabaseName);
						}
					}
				}
				DatabaseIndex = NextDatabaseIndex++;
			}

			DatabaseName = $"{MongoDbDatabaseNamePrefix}{DatabaseIndex}";
			ConnectionString = $"{_mongoDbRunner.GetConnectionString()}/{DatabaseName}";
			Client = new MongoClient(MongoClientSettings.FromConnectionString(ConnectionString));
		}

		public void Dispose()
		{
			Client.DropDatabase(DatabaseName);
		}
	}

	public class DatabaseIntegrationTest : IDisposable
    {
		private static object LockObject = new object();
		private MongoDbInstance? MongoDbInstance;
		private DatabaseService? DatabaseService;
		
        private static RedisRunner? _redisRunner;
        private static IDatabase? _redisDb;
        private static RedisConnectionPool? _redisConnectionPool;
        private static ConnectionMultiplexer? _conMux;
        public const int RedisDbNum = 15;

		public DatabaseIntegrationTest()
		{
		}

		public void Dispose()
		{
			MongoDbInstance?.Dispose();
			DatabaseService?.Dispose();
		}

		public DatabaseService GetDatabaseService()
        {
			lock(LockObject)
			{
				if (DatabaseService == null)
				{
					MongoDbInstance = new MongoDbInstance();

					ServerSettings Ss = new ServerSettings();
					Ss.DatabaseName = MongoDbInstance.DatabaseName;
					Ss.DatabaseConnectionString = MongoDbInstance.ConnectionString;

					ILoggerFactory LoggerFactory = new LoggerFactory();

					DatabaseService = new DatabaseService(Options.Create(Ss), LoggerFactory);
				}
			}
			return DatabaseService;
        }

        public static IDatabase GetRedisDatabase()
        {
			lock (LockObject)
			{
				if (_redisDb == null)
				{
					// One-time setup per test run to avoid overhead of starting the external Redis process
					_redisRunner = new RedisRunner();
					_redisRunner.Start();

					(string Host, int Port) = _redisRunner.GetListenAddress();
					_conMux = ConnectionMultiplexer.Connect($"{Host}:{Port},allowAdmin=true");
					ClearRedisDatabase();

					_redisDb = _conMux.GetDatabase(RedisDbNum);
				}
			}

	        return _redisDb;
        }
        
        public static RedisConnectionPool GetRedisConnectionPool()
        {
	        if (_redisConnectionPool != null)
	        {
		        return _redisConnectionPool;
	        }
	        
	        // Will initialize the Redis runner
	        GetRedisDatabase();

	        if (_redisRunner == null)
	        {
		        throw new Exception("Redis runner should have been initialized");
	        }

	        (string Host, int Port) = _redisRunner.GetListenAddress();
	        _redisConnectionPool = new RedisConnectionPool(3, $"{Host}:{Port},allowAdmin=true", RedisDbNum);
	        return _redisConnectionPool;
        }

        public static void ClearRedisDatabase()
        {
	        if (_conMux != null)
	        {
		        foreach (EndPoint Endpoint in _conMux.GetEndPoints())
		        {
			        _conMux.GetServer(Endpoint).FlushDatabase(RedisDbNum);
		        }    
	        }
        }

		public static T Deref<T>(T? Item)
		{
			Assert.IsNotNull(Item);
			return Item!;
		}
	}
}