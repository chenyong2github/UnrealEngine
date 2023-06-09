// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Amazon.AutoScaling;
using Amazon.AutoScaling.Model;
using Horde.Server.Agents;
using Horde.Server.Agents.Fleet;
using Horde.Server.Agents.Pools;
using HordeCommon;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

namespace Horde.Server.Tests.Fleet;

[TestClass]
public class LifecycleHookMessageTest
{
	[TestMethod]
	public void DeserializeLifecycleHookRequest()
	{
		string rawText = @"
{
	""version"": ""0"",
	""id"": ""d2b0a72e-03a4-11ee-be56-0242ac120002"",
	""detail-type"": ""EC2 Instance-terminate Lifecycle Action"",
	""source"": ""aws.autoscaling"",
	""account"": ""111111111111"",
	""time"": ""2023-06-05T10:48:08Z"",
	""region"": ""us-east-1"",
	""resources"": [""arn:aws:autoscaling:us-east-1:111111111111:autoScalingGroup:d816d436-03a4-11ee-be56-0242ac120002:autoScalingGroupName/my-asg-name""],
	""detail"": {
		""LifecycleActionToken"": ""f191f274-03a4-11ee-be56-0242ac120002"",
		""AutoScalingGroupName"": ""my-asg-name"",
		""LifecycleHookName"": ""my-hook-name"",
		""EC2InstanceId"": ""i-11234567890abcdef"",
		""LifecycleTransition"": ""autoscaling:EC2_INSTANCE_TERMINATING"",
		""Origin"": ""AutoScalingGroup"",
		""Destination"": ""EC2""
	}
}
";

		LifecycleActionRequest? req = JsonSerializer.Deserialize<LifecycleActionRequest>(rawText);
		Assert.AreEqual("f191f274-03a4-11ee-be56-0242ac120002", req!.Event.LifecycleActionToken);
		Assert.AreEqual("my-asg-name", req!.Event.AutoScalingGroupName);
		Assert.AreEqual("my-hook-name", req!.Event.LifecycleHookName);
		Assert.AreEqual("i-11234567890abcdef", req!.Event.Ec2InstanceId);
		Assert.AreEqual("autoscaling:EC2_INSTANCE_TERMINATING", req!.Event.LifecycleTransition);
		Assert.AreEqual("AutoScalingGroup", req!.Event.Origin);
	}
}

[TestClass]
public class AwsAutoScalingLifecycleServiceTest : TestSetup
{
	private AwsAutoScalingLifecycleService _asgLifecycleService = default!;
	private Mock<IAmazonAutoScaling> _asgMock = default!;
	private readonly List<CompleteLifecycleActionRequest> _lifecycleUpdates = new();

	protected override void Dispose(bool disposing)
	{
		if (disposing)
		{
			_asgLifecycleService?.Dispose();
		}

		base.Dispose(disposing);
	}

	[TestInitialize]
	public async Task Setup()
	{
		ILogger<AwsAutoScalingLifecycleService> logger = ServiceProvider.GetRequiredService<ILogger<AwsAutoScalingLifecycleService>>();
		_asgMock = new Mock<IAmazonAutoScaling>(MockBehavior.Strict);

		Task<CompleteLifecycleActionResponse> OnCompleteLifecycleActionAsync(CompleteLifecycleActionRequest request, CancellationToken cancellationToken)
		{
			_lifecycleUpdates.Add(request);
			return Task.FromResult(new CompleteLifecycleActionResponse() { HttpStatusCode = HttpStatusCode.OK });
		}

		_asgMock
			.Setup(x => x.CompleteLifecycleActionAsync(It.IsAny<CompleteLifecycleActionRequest>(), It.IsAny<CancellationToken>()))
			.Returns(OnCompleteLifecycleActionAsync);
		
		_asgLifecycleService = new AwsAutoScalingLifecycleService(AgentService, GetRedisServiceSingleton(), AgentCollection, Clock, ServiceProvider, Tracer, logger);
		_asgLifecycleService.SetAmazonAutoScalingClient(_asgMock.Object);
		await _asgLifecycleService.StartAsync(CancellationToken.None);
	}

	[TestMethod]
	public async Task TerminationRequested_WithAgentInService_MarkedForShutdown()
	{
		// Arrange
		LifecycleActionEvent lae = new() { Ec2InstanceId = "i-1234", LifecycleActionToken = "action-token-test", Origin = "AutoScalingGroup" };
		IAgent agent = await CreateAgentAsync(new PoolId("pool1"), properties: new() { KnownPropertyNames.AwsInstanceId + "=" + lae.Ec2InstanceId });

		// Act
		await AwsAsgLifecycleService.InitiateTerminationAsync(lae, CancellationToken.None);
		
		// Assert
		agent = (await AgentService.GetAgentAsync(agent.Id))!;
		Assert.IsTrue(agent.RequestShutdown);
	}
	
	[TestMethod]
	public async Task TerminationRequested_WithAgentOnline_LifecycleIsContinued()
	{
		// Arrange
		LifecycleActionEvent lae = new() { Ec2InstanceId = "i-1234", LifecycleActionToken = "action-token-test", Origin = "AutoScalingGroup" };
		await CreateAgentAsync(new PoolId("pool1"), properties: new() { KnownPropertyNames.AwsInstanceId + "=" + lae.Ec2InstanceId });
		await _asgLifecycleService.InitiateTerminationAsync(lae, CancellationToken.None);
		
		// Act
		await Clock.AdvanceAsync(_asgLifecycleService.LifecycleUpdaterInterval + TimeSpan.FromSeconds(10));
		
		// Assert
		AssertLifecycleUpdate(lae, AwsAutoScalingLifecycleService.ActionContinue, _lifecycleUpdates[0]);
	}
	
	[TestMethod]
	public async Task TerminationRequested_WithAgentGoingFromOnlineToOffline()
	{
		// Arrange
		LifecycleActionEvent lae = new() { Ec2InstanceId = "i-1234", LifecycleActionToken = "action-token-test", Origin = "AutoScalingGroup" };
		List<string> props = new() { KnownPropertyNames.AwsInstanceId + "=" + lae.Ec2InstanceId };
		IAgent agent = await CreateAgentAsync(new PoolId("pool1"), properties: props);
		agent = await AgentService.CreateSessionAsync(agent, AgentStatus.Ok, props, new Dictionary<string, int>(), "v1");
		await _asgLifecycleService.InitiateTerminationAsync(lae, CancellationToken.None);
		TimeSpan extraMargin = TimeSpan.FromSeconds(10);
		
		// Agent is online (e.g still running a job) and the lifecycle update tick is triggered
		await Clock.AdvanceAsync(_asgLifecycleService.LifecycleUpdaterInterval + extraMargin);
		AssertLifecycleUpdate(lae, AwsAutoScalingLifecycleService.ActionContinue, _lifecycleUpdates[0]);
		
		// Agent still online
		await Clock.AdvanceAsync(_asgLifecycleService.LifecycleUpdaterInterval + extraMargin);
		AssertLifecycleUpdate(lae, AwsAutoScalingLifecycleService.ActionContinue, _lifecycleUpdates[1]);
		
		// Agent responded to shutdown request, now safe to notify AWS ASG the termination can take place
		agent = await AgentService.CreateSessionAsync(agent, AgentStatus.Stopped, props, new Dictionary<string, int>(), "v1");
		await Clock.AdvanceAsync(_asgLifecycleService.LifecycleUpdaterInterval + extraMargin);
		AssertLifecycleUpdate(lae, AwsAutoScalingLifecycleService.ActionAbandon, _lifecycleUpdates[2]);
		
		// Trigger lifecycle update tick once again and no further updates should have been sent
		await Clock.AdvanceAsync(_asgLifecycleService.LifecycleUpdaterInterval + extraMargin);
		Assert.AreEqual(3, _lifecycleUpdates.Count);
	}
	
	[TestMethod]
	public async Task TerminationRequested_WithAgentInWarmPool_LifecycleIsAbandoned()
	{
		// Arrange
		LifecycleActionEvent lae = new() { Ec2InstanceId = "i-1234", LifecycleActionToken = "action-token-test", Origin = "WarmPool" };

		// Act
		await _asgLifecycleService.InitiateTerminationAsync(lae, CancellationToken.None);
		
		// Assert
		AssertLifecycleUpdate(lae, AwsAutoScalingLifecycleService.ActionAbandon, _lifecycleUpdates[0]);
	}

	private static void AssertLifecycleUpdate(LifecycleActionEvent expectedEvent, string expectedResult, CompleteLifecycleActionRequest actual)
	{
		Assert.AreEqual(expectedEvent.Ec2InstanceId, actual.InstanceId);
		Assert.AreEqual(expectedResult, actual.LifecycleActionResult);
		Assert.AreEqual(expectedEvent.LifecycleActionToken, actual.LifecycleActionToken);
		Assert.AreEqual(expectedEvent.LifecycleHookName, actual.LifecycleHookName);
		Assert.AreEqual(expectedEvent.AutoScalingGroupName, actual.AutoScalingGroupName);
	}
}
