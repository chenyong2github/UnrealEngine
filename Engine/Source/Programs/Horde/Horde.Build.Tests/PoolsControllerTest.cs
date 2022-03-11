// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Fleet.Autoscale;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
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
        public async Task CreatePoolsTest()
        {
	        CreatePoolRequest Request = new CreatePoolRequest
	        {
		        Name = "Pool1",
		        ScaleOutCooldown = 111,
		        ScaleInCooldown = 222,
		        SizeStrategy = PoolSizeStrategy.JobQueue,
		        JobQueueSettings = new HordeServer.Api.JobQueueSettings(new Horde.Build.Fleet.Autoscale.JobQueueSettings(0.35, 0.85))
	        };
	        
	        ActionResult<CreatePoolResponse> Result = await PoolsController.CreatePoolAsync(Request);
	        CreatePoolResponse Response = Result.Value!;
		        
	        IPool Pool = (await PoolService.GetPoolAsync(new StringId<IPool>(Response.Id)))!;
	        Assert.AreEqual(Request.Name, Pool.Name);
	        Assert.AreEqual(Request.ScaleOutCooldown, (int)Pool.ScaleOutCooldown!.Value.TotalSeconds);
	        Assert.AreEqual(Request.ScaleInCooldown, (int)Pool.ScaleInCooldown!.Value.TotalSeconds);
	        Assert.AreEqual(Request.JobQueueSettings.ScaleOutFactor, Pool.JobQueueSettings!.ScaleOutFactor, 0.0001);
	        Assert.AreEqual(Request.JobQueueSettings.ScaleInFactor, Pool.JobQueueSettings!.ScaleInFactor, 0.0001);
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