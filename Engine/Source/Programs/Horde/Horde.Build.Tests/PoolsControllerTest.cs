// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Fleet.Autoscale;
using HordeServer.Api;
using HordeServer.Models;
using HordeServerTests;
using Microsoft.AspNetCore.Mvc;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
    [TestClass]
    public class PoolsControllerTest : TestSetup
    {
        [TestMethod]
        public async Task GetPoolsTest()
        {
	        IPool Pool1 = await PoolService.CreatePoolAsync("Pool1", Properties: new() { {"foo", "bar"}, {"lorem", "ipsum"} });
	        ActionResult<List<object>> RawResult = await PoolsController.GetPoolsAsync();
	        Assert.AreEqual(1, RawResult.Value!.Count);
	        GetPoolResponse Response = (RawResult.Value![0] as GetPoolResponse)!;
	        Assert.AreEqual(Pool1.Id.ToString(), Response.Id);
	        Assert.AreEqual(Pool1.Name, Response.Name);
	        Assert.AreEqual(Pool1.SizeStrategy, Response.SizeStrategy);
        }
        
        [TestMethod]
        public async Task UpdatePoolTest()
        {
	        IPool Pool1 = await PoolService.CreatePoolAsync("Pool1", Properties: new() { {"foo", "bar"}, {"lorem", "ipsum"} });

	        await PoolsController.UpdatePoolAsync(Pool1.Id.ToString(), new UpdatePoolRequest
	        {
		        Name = "Pool1Modified",
		        SizeStrategy = PoolSizeStrategy.JobQueue
	        });

	        ActionResult<object> GetResult = await PoolsController.GetPoolAsync(Pool1.Id.ToString());
	        GetPoolResponse Response = (GetResult.Value! as GetPoolResponse)!;
	        Assert.AreEqual("Pool1Modified", Response.Name);
	        Assert.AreEqual(PoolSizeStrategy.JobQueue, Response.SizeStrategy);
        }
    }
}