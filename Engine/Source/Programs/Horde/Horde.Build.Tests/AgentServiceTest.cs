// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Agents;
using Horde.Build.Jobs;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.AspNetCore.Mvc;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests;

/// <summary>
///     Testing the agent service
/// </summary>
[TestClass]
public class AgentServiceTest : TestSetup
{
	[TestMethod]
	public async Task GetJobs()
	{
		await CreateFixtureAsync();

		ActionResult<List<object>> res = await JobsController.FindJobsAsync();

		List<GetJobResponse> responses = res.Value!.ConvertAll(x => (GetJobResponse)x);
		responses.SortBy(x => x.Change);

		Assert.AreEqual(2, responses.Count);
		Assert.AreEqual("hello1", responses[0].Name);
		Assert.AreEqual("hello2", responses[1].Name);

		res = await JobsController.FindJobsAsync(includePreflight: false);
		Assert.AreEqual(1, res.Value!.Count);
		Assert.AreEqual("hello2", (res.Value[0] as GetJobResponse)!.Name);
	}

	[TestMethod]
	public async Task CreateSessionTest()
	{
		Fixture fixture = await CreateFixtureAsync();

		IAgent agent = await AgentService.CreateSessionAsync(fixture.Agent1, AgentStatus.Ok, new List<string>(),
			new Dictionary<string, int>(),
			"test");

		Assert.IsTrue(AgentService.AuthorizeSession(agent, GetUser(agent)));
		await Clock.AdvanceAsync(TimeSpan.FromMinutes(20));
		Assert.IsFalse(AgentService.AuthorizeSession(agent, GetUser(agent)));
	}

	private static ClaimsPrincipal GetUser(IAgent agent)
	{
		return new ClaimsPrincipal(new ClaimsIdentity(
			new List<Claim> { new(HordeClaimTypes.AgentSessionId, agent.SessionId.ToString()!) }, "TestAuthType"));
	}
}