// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Agents;
using Horde.Build.Auditing;
using Horde.Build.Jobs;
using Horde.Build.Server;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
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
	
	[TestMethod]
	public async Task AuditLogAwsInstanceTypeChanges()
	{
		Fixture fixture = await CreateFixtureAsync();
		IAuditLogChannel<AgentId> agentLogger = AgentCollection.GetLogger(fixture.Agent1.Id);

		async Task<bool> AuditLogContains(string text)
		{
			await FlushAuditLogsAsync();
			return await agentLogger.FindAsync().AnyAsync(x => x.Data.Contains(text, StringComparison.Ordinal));
		}
		
		List<string> props = new () { $"{KnownPropertyNames.AwsInstanceType}=m5.large" };
		IAgent agent = await AgentService.CreateSessionAsync(fixture.Agent1, AgentStatus.Ok, props, new Dictionary<string, int>(), "test");
		Assert.IsFalse(await AuditLogContains("AWS EC2 instance type changed"));
		
		props = new () { $"{KnownPropertyNames.AwsInstanceType}=c6.xlarge" };
		agent = await AgentService.CreateSessionAsync(agent, AgentStatus.Ok, props, new Dictionary<string, int>(), "test");
		Assert.IsTrue(await AuditLogContains("AWS EC2 instance type changed"));
	}

	private Task FlushAuditLogsAsync()
	{
		IAuditLog<AgentId> auditLog = ServiceProvider.GetRequiredService<IAuditLog<AgentId>>();
		AuditLog<AgentId> log = (AuditLog<AgentId>)auditLog;
		return log.FlushMessagesInternalAsync();
	}
	
	[TestMethod]
	public async Task GetAgentRateTest()
	{
		IAgent agent1 = await AgentService.CreateAgentAsync("agent1", true, null);
		IAgent agent2 = await AgentService.CreateAgentAsync("agent2", true, null);
		await AgentService.CreateSessionAsync(agent1, AgentStatus.Ok, new List<string>() { "aws-instance-type=c5.24xlarge", "osfamily=windows" }, new Dictionary<string, int>(), "test");
		await AgentService.CreateSessionAsync(agent2, AgentStatus.Ok, new List<string>() { "aws-instance-type=c4.4xLARge", "osfamily=WinDowS" }, new Dictionary<string, int>(), "test");
		
		List<AgentRateConfig> agentRateConfigs = new()
		{
			new AgentRateConfig() { Condition = "aws-instance-type == 'c5.24xlarge' && osfamily == 'windows'", Rate = 200, },
			new AgentRateConfig() { Condition = "aws-instance-type == 'c4.4xlarge' && osfamily == 'windows'", Rate = 300 }
			
		};
		await AgentService.UpdateRateTableAsync(agentRateConfigs);
		
		double? rate1 = await AgentService.GetRateAsync(agent1.Id);
		Assert.AreEqual(200, rate1!.Value, 0.1);
		
		double? rate2 = await AgentService.GetRateAsync(agent2.Id);
		Assert.AreEqual(300, rate2!.Value, 0.1);
	}

	private static ClaimsPrincipal GetUser(IAgent agent)
	{
		return new ClaimsPrincipal(new ClaimsIdentity(
			new List<Claim> { new(HordeClaimTypes.AgentSessionId, agent.SessionId.ToString()!) }, "TestAuthType"));
	}
}