// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace HordeServerTests
{
    [TestClass]
    public class DatabaseRunnerTest
    {
        [TestMethod]
        public void RunMongoDbTest()
        {
	        MongoDbRunnerLocal Runner = new MongoDbRunnerLocal();
	        Runner.Start();
            Thread.Sleep(100);
            Runner.Stop();
        }
        
        [TestMethod]
        public void RunRedisTest()
        {
	        RedisRunner Runner = new RedisRunner();
	        Runner.Start();
	        Thread.Sleep(100);
	        Runner.Stop();
        }
    }
}