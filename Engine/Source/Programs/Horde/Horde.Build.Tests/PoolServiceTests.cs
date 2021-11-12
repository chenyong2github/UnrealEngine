// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Collections.Impl;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServerTests
{
    [TestClass]
    public class PoolServiceTests : TestSetup
    {
        private readonly Dictionary<string, string> FixtureProps = new Dictionary<string, string>
        {
            {"foo", "bar"},
            {"lorem", "ipsum"}
        };

        public PoolServiceTests()
        {
        }

        private async Task<IPool> CreatePoolFixture(String Name)
        {
            return await PoolService.CreatePoolAsync(Name, Properties: FixtureProps);
        }

        [TestMethod]
        public async Task GetPoolTest()
        {
            Assert.IsNull(await PoolService.GetPoolAsync(new PoolId("this-does-not-exist")));

            var NewPool = await CreatePoolFixture("create-pool");
            var Pool = await PoolService.GetPoolAsync(NewPool.Id);
			Assert.IsNotNull(Pool);
            Assert.AreEqual("create-pool", Pool!.Id.ToString());
            Assert.AreEqual("create-pool", Pool.Name);
            Assert.AreEqual(FixtureProps.Count, Pool.Properties.Count);
            Assert.AreEqual(FixtureProps["foo"], Pool.Properties["foo"]);
            Assert.AreEqual(FixtureProps["lorem"], Pool.Properties["lorem"]);
        }
        
        [TestMethod]
        public async Task GetPoolsTest()
        {
            await GetDatabaseService().Database.DropCollectionAsync("Pools");
            
            var Pools = await PoolService.GetPoolsAsync();
            Assert.AreEqual(Pools.Count, 0);
            
            var Pool0 = await CreatePoolFixture("multiple-pools-0");
            var Pool1 = await CreatePoolFixture("multiple-pools-1");
            Pools = await PoolService.GetPoolsAsync();
            Assert.AreEqual(Pools.Count, 2);
            Assert.AreEqual(Pools[0].Name, Pool0.Name);
            Assert.AreEqual(Pools[1].Name, Pool1.Name);
        }
        
        [TestMethod]
        public async Task DeletePoolTest()
        {
            Assert.IsFalse(await PoolService.DeletePoolAsync(new PoolId("this-does-not-exist")));
            var Pool = await CreatePoolFixture("pool-to-be-deleted");
            Assert.IsTrue(await PoolService.DeletePoolAsync(Pool.Id));
        }
        
        [TestMethod]
        public async Task UpdatePoolTest()
        {
			string UniqueSuffix = Guid.NewGuid().ToString("N");
            var Pool = await CreatePoolFixture($"update-pool-{UniqueSuffix}");
            var UpdatedProps = new Dictionary<string, string?>
            {
                {"foo", "bar"},
                {"lorem", null}, // This entry will get removed
                {"cookies", "yumyum"},
            };

            var UpdatedPool = await PoolService.UpdatePoolAsync(Pool, $"update-pool-new-name-{UniqueSuffix}", NewProperties: UpdatedProps);
			Assert.IsNotNull(UpdatedPool);
            Assert.AreEqual(Pool.Id, UpdatedPool!.Id);
            Assert.AreEqual($"update-pool-new-name-{UniqueSuffix}", UpdatedPool.Name);
            Assert.AreEqual(UpdatedPool.Properties.Count, 2);
            Assert.AreEqual(UpdatedProps["foo"], UpdatedPool.Properties["foo"]);
            Assert.AreEqual(UpdatedProps["cookies"], UpdatedPool.Properties["cookies"]);
        }
        
        [TestMethod]
        public async Task UpdatePoolCollectionTest()
        {
	        IPool Pool = await CreatePoolFixture("update-pool-2");
	        await PoolCollection.TryUpdateAsync(Pool, LastScaleUpTime: DateTime.UtcNow);
        }
    }
}