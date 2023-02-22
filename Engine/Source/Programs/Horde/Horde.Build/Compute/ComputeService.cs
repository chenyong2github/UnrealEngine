// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Serialization;
using Horde.Build.Agents;
using Horde.Build.Agents.Pools;
using Horde.Build.Server;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Options;

namespace Horde.Build.Compute
{
	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	public class ComputeService
	{
		class RequestQueue
		{
			public IoHash Hash { get; }
			public Requirements Requirements { get; }
			public Queue<ComputeRequest> Requests { get; } = new Queue<ComputeRequest>();

			public RequestQueue(IoHash hash, Requirements requirements)
			{
				Hash = hash;
				Requirements = requirements;
			}
		}

		class ClusterInfo
		{
			public Dictionary<IoHash, RequestQueue> Queues { get; } = new Dictionary<IoHash, RequestQueue>();
		}

		readonly object _lockObject = new object();
		readonly IOptionsMonitor<GlobalConfig> _globalConfig;
		readonly Dictionary<ClusterId, ClusterInfo> _clusters = new Dictionary<ClusterId, ClusterInfo>();

		/// <summary>
		/// Constructor
		/// </summary>
		protected ComputeService(IOptionsMonitor<GlobalConfig> globalConfig)
		{
			_globalConfig = globalConfig;
		}

		/// <summary>
		/// Adds a new compute request
		/// </summary>
		/// <param name="clusterId">The compute cluster id</param>
		/// <param name="requirements">Requirements for the agent to serve the request</param>
		/// <param name="task">Connection information for the compute channel</param>
		public void AddRequest(ClusterId clusterId, Requirements requirements, ComputeTask task)
		{
			IoHash hash = IoHash.Compute(requirements.Serialize());
			lock (_lockObject)
			{
				ClusterInfo? cluster;
				if (!_clusters.TryGetValue(clusterId, out cluster))
				{
					cluster = new ClusterInfo();
					_clusters.Add(clusterId, cluster);
				}

				RequestQueue? queue;
				if (!cluster.Queues.TryGetValue(hash, out queue))
				{
					queue = new RequestQueue(hash, requirements);
					cluster.Queues.Add(hash, queue);
				}

				queue.Requests.Enqueue(new ComputeRequest(requirements, task));
			}
		}

		/// <summary>
		/// Attempts to pop a request from the queue which can be met by the given agent
		/// </summary>
		/// <param name="agent">Agent to allocate a request for</param>
		/// <returns>The allocated request</returns>
		public ComputeRequest? PopRequest(IAgent agent)
		{
			ComputeRequest? request = null;
			lock (_lockObject)
			{
				ClusterId? removeClusterId = null;

				GlobalConfig globalConfig = _globalConfig.CurrentValue;
				foreach ((ClusterId clusterId, ClusterInfo clusterInfo) in _clusters)
				{
					ComputeClusterConfig? clusterConfig;
					if (!globalConfig.TryGetComputeCluster(clusterId, out clusterConfig))
					{
						removeClusterId ??= clusterId;
					}
					else if (clusterConfig.Condition == null || agent.SatisfiesCondition(clusterConfig.Condition))
					{
						foreach ((IoHash hash, RequestQueue queue) in clusterInfo.Queues)
						{
							if (agent.MeetsRequirements(queue.Requirements))
							{
								request = queue.Requests.Dequeue();

								if (queue.Requests.Count == 0)
								{
									clusterInfo.Queues.Remove(hash);
								}
								break;
							}
						}

						if (clusterInfo.Queues.Count == 0)
						{
							removeClusterId ??= clusterId;
						}
					}
				}

				if (removeClusterId != null)
				{
					_clusters.Remove(removeClusterId.Value);
				}
			}

			return request;
		}
	}
}
