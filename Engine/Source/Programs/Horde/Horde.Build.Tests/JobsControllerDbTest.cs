// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;
using HordeServer.Api;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Mvc;

namespace HordeServerTests
{
    /// <summary>
    /// Database-only test for testing the Job controller. Different from the JobsController test that set up
    /// the entire ASP.NET chain.
    /// </summary>
	[TestClass]
    public class JobsControllerDbTest : TestSetup
    {
        
        [TestMethod]
        public async Task GetJobs()
        {
			Fixture Fixture = await CreateFixtureAsync();

			ActionResult<List<object>> Res = await JobsController.FindJobsAsync();
	        Assert.AreEqual(2, Res.Value!.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
	        Assert.AreEqual("hello1", (Res.Value[1] as GetJobResponse)!.Name);
	        
	        Res = await JobsController.FindJobsAsync(IncludePreflight: false);
	        Assert.AreEqual(1, Res.Value!.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
        }
        
        [TestMethod]
        public async Task AbortStepTest()
        {
			Fixture Fixture = await CreateFixtureAsync();

	        IJob Job = Fixture.Job1;
	        SubResourceId BatchId = Job.Batches[0].Id;
	        SubResourceId StepId = Job.Batches[0].Steps[0].Id;

	        object Obj = (await JobsController.GetStepAsync(Job.Id, BatchId.ToString(), StepId.ToString())).Value!;
	        GetStepResponse StepRes = (Obj as GetStepResponse)!;
	        Assert.IsFalse(StepRes.AbortRequested);
	        
	        UpdateStepRequest UpdateReq = new UpdateStepRequest();
	        UpdateReq.AbortRequested = true;
	        Obj = (await JobsController.UpdateStepAsync(Job.Id, BatchId.ToString(), StepId.ToString(), UpdateReq)).Value!;
	        UpdateStepResponse UpdateRes = (Obj as UpdateStepResponse)!;
	        
	        Obj = (await JobsController.GetStepAsync(Job.Id, BatchId.ToString(), StepId.ToString())).Value!;
	        StepRes = (Obj as GetStepResponse)!;
	        Assert.IsTrue(StepRes.AbortRequested);
//	        Assert.AreEqual("Anonymous", StepRes.AbortByUser);
        }
    }
}