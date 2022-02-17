// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon;
using Amazon.EC2;
using Amazon.EC2.Model;
using HordeServer.Collections;
using HordeServer.Models;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Utilities;
using OpenTracing;
using OpenTracing.Util;

namespace HordeServer.Services.Impl
{
	/// <summary>
	/// Fleet manager for handling AWS EC2 instances
	/// </summary>
	public sealed class AwsFleetManager : IFleetManager, IDisposable
	{
		const string AwsTagPropertyName = "aws-tag";
		const string PoolTagName = "Horde_Autoscale_Pool";

		AmazonEC2Client Client;
		IAgentCollection AgentCollection;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public AwsFleetManager(IAgentCollection AgentCollection, ILogger<AwsFleetManager> Logger)
		{
			this.AgentCollection = AgentCollection;
			this.Logger = Logger;

			AmazonEC2Config Config = new AmazonEC2Config();
			Config.RegionEndpoint = RegionEndpoint.USEast1;

			Logger.LogInformation("Initializing AWS fleet manager for region {Region}", Config.RegionEndpoint);

			Client = new AmazonEC2Client(Config);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
		}

		/// <inheritdoc/>
		public async Task ExpandPoolAsync(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("ExpandPool").StartActive();
			Scope.Span.SetTag("poolName", Pool.Name);
			Scope.Span.SetTag("numAgents", Agents.Count);
			Scope.Span.SetTag("count", Count);

			DescribeInstancesResponse DescribeResponse;
			using (IScope DescribeScope = GlobalTracer.Instance.BuildSpan("DescribeInstances").StartActive())
			{
				// Find stopped instances in the correct pool
				DescribeInstancesRequest DescribeRequest = new DescribeInstancesRequest();
				DescribeRequest.Filters = new List<Filter>();
				DescribeRequest.Filters.Add(new Filter("instance-state-name", new List<string> { InstanceStateName.Stopped.Value }));
				DescribeRequest.Filters.Add(new Filter("tag:" + PoolTagName, new List<string> { Pool.Name }));
				DescribeResponse = await Client.DescribeInstancesAsync(DescribeRequest);
				DescribeScope.Span.SetTag("res.statusCode", (int)DescribeResponse.HttpStatusCode);
				DescribeScope.Span.SetTag("res.numReservations", DescribeResponse.Reservations.Count);
			}

			using (IScope StartScope = GlobalTracer.Instance.BuildSpan("StartInstances").StartActive())
			{
				// Try to start the given instances
				StartInstancesRequest StartRequest = new StartInstancesRequest();
				StartRequest.InstanceIds.AddRange(DescribeResponse.Reservations.SelectMany(x => x.Instances).Select(x => x.InstanceId).Take(Count));
				
				StartScope.Span.SetTag("req.instanceIds", string.Join(",", StartRequest.InstanceIds));
				if (StartRequest.InstanceIds.Count > 0)
				{
					StartInstancesResponse StartResponse = await Client.StartInstancesAsync(StartRequest);
					StartScope.Span.SetTag("res.statusCode", (int)StartResponse.HttpStatusCode);
					StartScope.Span.SetTag("res.numInstances", StartResponse.StartingInstances.Count);
					if ((int)StartResponse.HttpStatusCode >= 200 && (int)StartResponse.HttpStatusCode <= 299)
					{
						foreach (InstanceStateChange InstanceChange in StartResponse.StartingInstances)
						{
							Logger.LogInformation("Starting instance {InstanceId} for pool {PoolId} (prev state {PrevState}, current state {CurrentState}", InstanceChange.InstanceId, Pool.Id, InstanceChange.PreviousState, InstanceChange.CurrentState);
						}
					}
				}
			}
		}

		/// <inheritdoc/>
		public async Task ShrinkPoolAsync(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("ShrinkPool").StartActive();
			Scope.Span.SetTag("poolName", Pool.Name);
			Scope.Span.SetTag("count", Count);
			
			string AwsTagProperty = $"{AwsTagPropertyName}={PoolTagName}:{Pool.Name}";
			
			// Sort the agents by number of active leases. It's better to shutdown agents currently doing nothing.
			List<IAgent> FilteredAgents = Agents.OrderBy(x => x.Leases.Count).ToList();
			List<IAgent> AgentsWithAwsTags = FilteredAgents.Where(x => x.HasProperty(AwsTagProperty)).ToList(); 
			List<IAgent> AgentsLimitedByCount = AgentsWithAwsTags.Take(Count).ToList();
			
			Scope.Span.SetTag("agents.num", Agents.Count);
			Scope.Span.SetTag("agents.filtered.num", FilteredAgents.Count);
			Scope.Span.SetTag("agents.withAwsTags.num", AgentsWithAwsTags.Count);
			Scope.Span.SetTag("agents.limitedByCount.num", AgentsLimitedByCount.Count);

			foreach (IAgent Agent in AgentsLimitedByCount)
			{
				IAuditLogChannel<AgentId> AgentLogger = AgentCollection.GetLogger(Agent.Id);
				if (await AgentCollection.TryUpdateSettingsAsync(Agent, bRequestShutdown: true, ShutdownReason: "Autoscaler") != null)
				{
					AgentLogger.LogInformation("Marked for shutdown due to autoscaling (currently {NumLeases} leases outstanding)", Agent.Leases.Count);
				}
				else
				{
					AgentLogger.LogError("Unable to mark agent for shutdown due to autoscaling");
				}
			}
		}

		/// <inheritdoc/>
		public async Task<int> GetNumStoppedInstancesAsync(IPool Pool)
		{
			// Find all instances in the pool
			DescribeInstancesRequest DescribeRequest = new DescribeInstancesRequest();
			DescribeRequest.Filters = new List<Filter>();
			DescribeRequest.Filters.Add(new Filter("instance-state-name", new List<string> { InstanceStateName.Stopped.Value }));
			DescribeRequest.Filters.Add(new Filter("tag:" + PoolTagName, new List<string> { Pool.Name }));

			DescribeInstancesResponse DescribeResponse = await Client.DescribeInstancesAsync(DescribeRequest);
			return DescribeResponse.Reservations.SelectMany(x => x.Instances).Select(x => x.InstanceId).Distinct().Count();
		}
	}
}
