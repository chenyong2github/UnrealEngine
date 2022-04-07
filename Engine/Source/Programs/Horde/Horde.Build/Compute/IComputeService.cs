// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Build.Models;
using Horde.Build.Tasks;
using Horde.Build.Utilities;

namespace Horde.Build.Compute
{
	/// <summary>
	/// Interface for the compute service
	/// </summary>
	public interface IComputeService : ITaskSource
	{
		/// <summary>
		/// Gets information about a compute cluster
		/// </summary>
		/// <param name="clusterId">Cluster to use for execution</param>
		Task<IComputeClusterInfo> GetClusterInfoAsync(ClusterId clusterId);

		/// <summary>
		/// Post tasks to be executed to a channel
		/// </summary>
		/// <param name="clusterId">Cluster to use for execution</param>
		/// <param name="channelId">Unique identifier of the client</param>
		/// <param name="taskRefIds">List of task hashes</param>
		/// <param name="requirementsHash">Requirements document for execution</param>
		/// <returns>Async task</returns>
		Task AddTasksAsync(ClusterId clusterId, ChannelId channelId, List<RefId> taskRefIds, CbObjectAttachment requirementsHash);

		/// <summary>
		/// Dequeue completed items from a queue and return immediately
		/// </summary>
		/// <param name="clusterId">Cluster containing the channel</param>
		/// <param name="channelId">Queue to remove items from</param>
		/// <returns>List of status updates</returns>
		Task<List<ComputeTaskStatus>> GetTaskUpdatesAsync(ClusterId clusterId, ChannelId channelId);

		/// <summary>
		/// Dequeue completed items from a queue
		/// </summary>
		/// <param name="clusterId">Cluster containing the channel</param>
		/// <param name="channelId">Queue to remove items from</param>
		/// <param name="cancellationToken">Cancellation token to stop waiting for items</param>
		/// <returns>List of status updates</returns>
		Task<List<ComputeTaskStatus>> WaitForTaskUpdatesAsync(ClusterId clusterId, ChannelId channelId, CancellationToken cancellationToken);
	}
}
