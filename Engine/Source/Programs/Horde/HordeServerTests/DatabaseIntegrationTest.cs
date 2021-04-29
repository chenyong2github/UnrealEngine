// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
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
    
    public class DatabaseIntegrationTest
    {
        private static MongoDbRunnerLocal? _mongoDbRunner;
        private static RedisRunner? _redisRunner;
        private static DatabaseService? _databaseService;
        private static IDatabase? _redisDb;
        private static RedisConnectionPool? _redisConnectionPool;
        private static ConnectionMultiplexer? _conMux;
        public const string MongoDbDatabaseName = "HordeServerTest";
        public const int RedisDbNum = 15;
        
        protected async Task<TestSetup> GetTestSetup()
        {
	        TestSetup TestSetup = new TestSetup(GetDatabaseService(true));
	        await TestSetup.CreateFixture(true);
	        return TestSetup;
        }

        public static DatabaseService GetDatabaseService(bool ForceDropDatabase = false)
        {
            if (_mongoDbRunner == null)
            {
                // One-time setup per test run to avoid overhead of starting the external MongoDB process
                
                Startup.ConfigureMongoDbClient();
                _mongoDbRunner = new MongoDbRunnerLocal(MongoDbDatabaseName);
                _mongoDbRunner.Start();
                
                ServerSettings Ss = new ServerSettings();
                Ss.DatabaseName = MongoDbDatabaseName;
                Ss.DatabaseConnectionString = _mongoDbRunner.GetConnectionString();
                TestOptionsMonitor<ServerSettings> SsMonitor = new TestOptionsMonitor<ServerSettings>(Ss);
            
                ILoggerFactory LoggerFactory = new LoggerFactory();
                ILogger<DatabaseService> Logger = LoggerFactory.CreateLogger<DatabaseService>();

                _databaseService = new DatabaseService(SsMonitor, Logger);
                _databaseService.Database.Client.DropDatabase(Ss.DatabaseName);
            }

            if (ForceDropDatabase)
            {
	            _databaseService!.Database.Client.DropDatabase(MongoDbDatabaseName);
            }

            return _databaseService!;
        }

        public static MongoDbRunnerLocal GetMongoDbRunner()
        {
            GetDatabaseService();
            return _mongoDbRunner!;
        }

        public static IDatabase GetRedisDatabase()
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
    }
}