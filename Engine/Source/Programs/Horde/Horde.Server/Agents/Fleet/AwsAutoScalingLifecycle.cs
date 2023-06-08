// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Amazon.AutoScaling;
using Amazon.AutoScaling.Model;
using Horde.Server.Server;
using Horde.Server.Utilities;
using HordeCommon;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using OpenTelemetry.Trace;
using StackExchange.Redis;

namespace Horde.Server.Agents.Fleet;

/// <summary>
/// Service handling callbacks and lifecycle events triggered by EC2 auto-scaling in AWS
/// </summary>
public sealed class AwsAutoScalingLifecycleService : IHostedService, IDisposable
{
	/// <summary>
	/// Lifecycle action for continue
	/// In the case of terminating instances, setting this result means the instance should *continue* be kept alive
	/// </summary>
	public const string ActionContinue = "CONTINUE";
	
	/// <summary>
	/// Lifecycle action for abandon
	/// In the case of terminating instances, setting this result means the instance should be *terminated*
	/// </summary>
	public const string ActionAbandon = "ABANDON";
	
	/// <summary>
	/// Warm pool origin
	/// </summary>
	public const string OriginWarmPool = "WarmPool";
	
	/// <summary>
	/// Normal auto-scaling group origin
	/// </summary>
	public const string OriginAsg = "AutoScalingGroup";
	
	/// <summary>
	/// How often instance lifecycles for auto-scaling should be updated with AWS 
	/// </summary>
	public TimeSpan LifecycleUpdaterInterval { get; } = TimeSpan.FromMinutes(20);

	private const string RedisKey = "aws-asg-lifecycle";
	private readonly AgentService _agentService;
	private readonly RedisService _redisService;
	private readonly IAgentCollection _agents;
	private readonly IClock _clock;
	private readonly Tracer _tracer;
	private readonly ILogger<AwsAutoScalingLifecycleService> _logger;
	private readonly ITicker _updateLifecyclesTicker;

#pragma warning disable CA2213 // Disposable fields should be disposed
	private IAmazonAutoScaling? _awsAutoScaling;
#pragma warning restore CA2213
	
	/// <summary>
	/// Constructor
	/// </summary>
	public AwsAutoScalingLifecycleService(
		AgentService agentService,
		RedisService redisService,
		IAgentCollection agents,
		IClock clock,
		IServiceProvider serviceProvider,
		Tracer tracer,
		ILogger<AwsAutoScalingLifecycleService> logger)
	{
		_agentService = agentService;
		_redisService = redisService;
		_agents = agents;
		_clock = clock;
		_tracer = tracer;
		_logger = logger;
		_awsAutoScaling = serviceProvider.GetService<IAmazonAutoScaling>();

		string tickerName = $"{nameof(AwsAutoScalingLifecycleService)}.{nameof(UpdateLifecyclesAsync)}";
		_updateLifecyclesTicker = clock.AddSharedTicker(tickerName, LifecycleUpdaterInterval, UpdateLifecyclesAsync, logger);
	}

	internal void SetAmazonAutoScalingClient(IAmazonAutoScaling awsAutoScaling)
	{
		_awsAutoScaling = awsAutoScaling;
	}
	
	/// <inheritdoc/>
	public async Task StartAsync(CancellationToken cancellationToken)
	{
		await _updateLifecyclesTicker.StartAsync();
	}

	/// <inheritdoc/>
	public async Task StopAsync(CancellationToken cancellationToken)
	{
		await _updateLifecyclesTicker.StopAsync();
	} 

	/// <inheritdoc/>
	public void Dispose()
	{
		_updateLifecyclesTicker.Dispose();
	}
	
	/// <summary>
	/// Gracefully terminate an EC2 instance running the Horde agent
	/// This call is initiated by an event coming from AWS auto-scaling group and will request the agent to shutdown.
	/// Since the agent can be busy running a job, it's possible it cannot be terminated immediately.
	/// The termination request from AWS is therefore tracked and updated continuously as the agent shutdown progresses. 
	/// </summary>
	/// <param name="e">Lifecycle action event received</param>
	/// <param name="cancellationToken">Cancellation token</param>
	/// <returns>True if handled</returns>
	public async Task<bool> InitiateTerminationAsync(LifecycleActionEvent e, CancellationToken cancellationToken)
	{
		if (e.Origin == OriginAsg)
		{
			string instanceIdProp = KnownPropertyNames.AwsInstanceId + "=" + e.Ec2InstanceId;
			IAgent? agent = (await _agentService.FindAgentsAsync(null, null, instanceIdProp, null, null)).FirstOrDefault();
			if (agent == null)
			{
				_logger.LogWarning("Lifecycle action received but no agent with instance ID {InstanceId} found", e.Ec2InstanceId);
				return false;
			}
		
			await _agents.TryUpdateSettingsAsync(agent, requestShutdown: true);
			await TrackAgentLifecycleAsync(agent.Id, e);
			return true;
		}
		else if (e.Origin == OriginWarmPool)
		{
			// EC2 instance in warm pool and not current in use by Horde
			await SendCompleteLifecycleActionAsync(e, ActionResult.Abandon, cancellationToken);
			return true;
		}
		else
		{
			_logger.LogWarning("Unknown origin {Origin}", e.Origin);
			return false;
		}
	}

	private async Task TrackAgentLifecycleAsync(AgentId agentId, LifecycleActionEvent e)
	{
		DateTime utcNow = _clock.UtcNow;
		AgentLifecycleInfo info = new(agentId.ToString(), utcNow, utcNow, e);
		using MemoryStream ms = new (500);
		await JsonSerializer.SerializeAsync(ms, info);
		await _redisService.GetDatabase().HashSetAsync(RedisKey, agentId.ToString(), ms.ToArray());
	}

	private async Task SendCompleteLifecycleActionAsync(LifecycleActionEvent laEvent, ActionResult result, CancellationToken cancellationToken)
	{
		if (_awsAutoScaling == null)
		{
			throw new Exception($"{nameof(IAmazonAutoScaling)} client not initialized! Cannot make AWS API call.");
		}
		
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(AwsAutoScalingLifecycleService)}.{nameof(SendCompleteLifecycleActionAsync)}");
		CompleteLifecycleActionRequest request = new()
		{
			InstanceId = laEvent.Ec2InstanceId,
			LifecycleActionResult = result == ActionResult.Continue ? ActionContinue : ActionAbandon,
			LifecycleActionToken = laEvent.LifecycleActionToken,
			LifecycleHookName = laEvent.LifecycleHookName,
			AutoScalingGroupName = laEvent.AutoScalingGroupName
		};
		span.SetAttribute("instanceId", request.InstanceId);
		span.SetAttribute("lifecycleActionResult", request.LifecycleActionResult);
		span.SetAttribute("lifecycleActionToken", request.LifecycleActionToken);
		span.SetAttribute("lifecycleHookName", request.LifecycleHookName);
		span.SetAttribute("autoScalingGroupName", request.AutoScalingGroupName);
		
		CompleteLifecycleActionResponse response = await _awsAutoScaling.CompleteLifecycleActionAsync(request, cancellationToken);
		span.SetAttribute("statusCode", response.HttpStatusCode.ToString());
	}
	
	/// <summary>
	/// Update AWS auto-scaling on the lifecycle status for each agent being tracked for shutdown
	/// </summary>
	/// <param name="cancellationToken">Cancellation token for the async task</param>
	/// <returns>Async task</returns>
	internal async ValueTask UpdateLifecyclesAsync(CancellationToken cancellationToken)
	{
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(AwsAutoScalingLifecycleService)}.{nameof(UpdateLifecyclesAsync)}");

		Dictionary<AgentId, AgentLifecycleInfo> infos = new();
		IDatabase redis = _redisService.GetDatabase();
		
		foreach (HashEntry entry in await redis.HashGetAllAsync(RedisKey))
		{
			try
			{
				AgentId agentId = new(entry.Name.ToString());
				AgentLifecycleInfo info = JsonSerializer.Deserialize<AgentLifecycleInfo>(entry.Value.ToString()) ?? throw new Exception("Deserialization returned null");
				infos[agentId] = info;
			}
			catch (Exception e)
			{
				_logger.LogWarning(e, "Unable to deserialize agent lifecycle info object from Redis. {Key} = {Value}", entry.Name.ToString(), entry.Value.ToString());
				await redis.HashDeleteAsync(RedisKey, entry.Name);
			}
		}

		span.SetAttribute("numTrackedAgents", infos.Count);

		List<Task> tasks = new();
		foreach (IAgent agent in await _agents.GetManyAsync(infos.Keys.ToList()))
		{
			ActionResult result = agent.Status == AgentStatus.Stopped ? ActionResult.Abandon : ActionResult.Continue;
			tasks.Add(Task.Run(async () =>
			{
				await SendCompleteLifecycleActionAsync(infos[agent.Id].Event, result, cancellationToken);
				if (result == ActionResult.Abandon)
				{
					await redis.HashDeleteAsync(RedisKey, agent.Id.ToString());
				}
			}, cancellationToken));
		}
		
		span.SetAttribute("numLifecycleActionsSent", tasks.Count);
		await Task.WhenAll(tasks);
	}

	private class AgentLifecycleInfo
	{
		public string AgentId { get; private set; }
		public DateTime CreatedAt { get; private set; }
		public DateTime UpdatedAt { get; private set; }
		public LifecycleActionEvent Event { get; private set; }

		public AgentLifecycleInfo(string agentId, DateTime createdAt, DateTime updatedAt, LifecycleActionEvent @event)
		{
			AgentId = agentId;
			CreatedAt = createdAt;
			UpdatedAt = updatedAt;
			Event = @event;
		}
	}

	private enum ActionResult
	{
		Continue, Abandon
	}
}

/// <summary>
/// An AWS EventBridge event containing a lifecycle hook message for an auto-scaling group
/// </summary>
public class LifecycleActionRequest
{
	/// <summary>
	/// Event
	/// </summary>
	[JsonPropertyName("detail")]
	public LifecycleActionEvent Event { get; set; }

	/// <summary>
	/// Constructor
	/// </summary>
	/// <param name="event"></param>
	public LifecycleActionRequest(LifecycleActionEvent @event)
	{
		Event = @event;
	}
}

/// <summary>
/// Lifecycle action event from AWS auto-scaling group
/// The property names are explicitly set to highlight these are not controlled by Horde but sent from AWS API.
/// </summary>
public class LifecycleActionEvent
{
	/// <summary>
	/// Name of the lifecycle hook
	/// </summary>
	[JsonPropertyName("LifecycleHookName")] public string LifecycleHookName { get; set; } = "";
	
	/// <summary>
	/// Token for the lifecycle action
	/// </summary>
	[JsonPropertyName("LifecycleActionToken")] public string LifecycleActionToken { get; set; } = "";
	
	/// <summary>
	/// Lifecycle transition
	/// </summary>
	[JsonPropertyName("LifecycleTransition")] public string LifecycleTransition { get; set; } = "";
	
	/// <summary>
	/// Name of the auto-scaling group being handled
	/// </summary>
	[JsonPropertyName("AutoScalingGroupName")] public string AutoScalingGroupName { get; set; } = "";
	
	/// <summary>
	/// Instance ID
	/// </summary>
	[JsonPropertyName("EC2InstanceId")] public string Ec2InstanceId { get; set; } = "";
	
	/// <summary>
	/// Origin of the instance (e.g an agent in service or in a warm pool)
	/// </summary>
	[JsonPropertyName("Origin")] public string Origin { get; set; } = "";
}

/// <summary>
/// Controller handling callbacks for AWS auto-scaling
/// </summary>
[ApiController]
[Authorize]
[Route("[controller]")]
public class AwsAutoScalingLifecycleController : HordeControllerBase
{
	private AwsAutoScalingLifecycleService _lifecycleService;

	/// <summary>
	/// Constructor
	/// </summary>
	public AwsAutoScalingLifecycleController(AwsAutoScalingLifecycleService lifecycleService)
	{
		_lifecycleService = lifecycleService;
	}
	
	/// <summary>
	/// Callback handling auto-scaling notification generated by the ASG
	/// A Lambda function should be configured first to receive the notification from AWS EventBridge.
	/// Then in turn, call the Horde server on this endpoint. 
	/// </summary>
	/// <param name="request">Request coming from</param>
	/// <returns>Human readable message</returns>
	[HttpPost]
	[Route("/api/v1/aws/asg-lifecycle-hook")]
	public async Task<ActionResult> LifecycleHookAsync([FromBody] LifecycleActionRequest request)
	{
		await _lifecycleService.InitiateTerminationAsync(request.Event, CancellationToken.None);
		return new JsonResult(new { message = "Handled" });
	}
}