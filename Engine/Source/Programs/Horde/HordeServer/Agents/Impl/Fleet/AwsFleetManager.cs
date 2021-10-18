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
		public async Task ExpandPool(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			// Find stopped instances in the correct pool
			DescribeInstancesRequest DescribeRequest = new DescribeInstancesRequest();
			DescribeRequest.Filters = new List<Filter>();
			DescribeRequest.Filters.Add(new Filter("instance-state-name", new List<string> { InstanceStateName.Stopped.Value }));
			DescribeRequest.Filters.Add(new Filter("tag:" + PoolTagName, new List<string> { Pool.Name }));
			DescribeInstancesResponse DescribeResponse = await Client.DescribeInstancesAsync(DescribeRequest);

			// Try to start the given instances
			StartInstancesRequest StartRequest = new StartInstancesRequest();
			StartRequest.InstanceIds.AddRange(DescribeResponse.Reservations.SelectMany(x => x.Instances).Select(x => x.InstanceId).Take(Count));

			if (StartRequest.InstanceIds.Count > 0)
			{
				StartInstancesResponse StartResponse = await Client.StartInstancesAsync(StartRequest);
				if ((int)StartResponse.HttpStatusCode >= 200 && (int)StartResponse.HttpStatusCode <= 299)
				{
					foreach (string InstanceId in StartRequest.InstanceIds)
					{
						Logger.LogInformation("Starting instance {InstanceId} for pool {PoolId}", InstanceId, Pool.Id);
					}
				}
			}
		}

		/// <inheritdoc/>
		public async Task ShrinkPool(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			string AwsTagProperty = $"{AwsTagPropertyName}={PoolTagName}:{Pool.Name}";

			// Sort the agents by number of active leases. It's better to shutdown agents currently doing nothing.
			List<IAgent> SortedAgents = Agents.OrderBy(x => x.Leases.Count).ToList();

			// Try to remove the agents
			for (int Idx = 0; Idx < SortedAgents.Count && Count > 0; Idx++)
			{
				IAgent Agent = SortedAgents[Idx];
				if (Agent.HasProperty(AwsTagProperty))
				{
					for (IAgent? NewAgent = Agent; NewAgent != null; NewAgent = await AgentCollection.GetAsync(Agent.Id))
					{
						if (await AgentCollection.TryUpdateSettingsAsync(NewAgent, bRequestShutdown: true) != null)
						{
							AgentCollection.GetLogger(Agent.Id).LogInformation("Shutting down due to autoscaler");
							Count--;
							break;
						}
					}
				}
			}
		}
	}
}
