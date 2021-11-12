// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using HordeServer.Api;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using HordeServer.Controllers;
using HordeServer.Models;
using Microsoft.AspNetCore.Mvc;

namespace HordeServerTests
{
	[TestClass]
    public class AgentControllerDbTest : TestSetup
    {
        [TestMethod]
        public async Task UpdateAgent()
        {
			Fixture Fixture = await CreateFixtureAsync();
	        IAgent FixtureAgent = Fixture.Agent1;

	        ActionResult<object> Obj = await AgentsController.GetAgentAsync(FixtureAgent.Id.ToString());
	        GetAgentResponse GetRes = (Obj.Value as GetAgentResponse)!;
	        Assert.AreEqual(Fixture!.Agent1Name.ToUpper(), GetRes.Name);
	        Assert.IsNull(GetRes.Comment);
	        
	        UpdateAgentRequest UpdateReq = new UpdateAgentRequest();
	        UpdateReq.Comment = "foo bar baz";
	        await AgentsController.UpdateAgentAsync(FixtureAgent.Id.ToString(), UpdateReq);
	        
	        Obj = await AgentsController.GetAgentAsync(FixtureAgent.Id.ToString());
	        GetRes = (Obj.Value as GetAgentResponse)!;
	        Assert.AreEqual("foo bar baz", GetRes.Comment);
        }
    }
}