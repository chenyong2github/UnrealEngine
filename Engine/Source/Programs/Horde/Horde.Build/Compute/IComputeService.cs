// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using HordeServer.Models;
using HordeServer.Storage;
using HordeServer.Tasks;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Compute
{
	using LeaseId = ObjectId<ILease>;

	/// <summary>
	/// Status of a compute task
	/// </summary>
	public interface IComputeTaskStatus
	{
		/// <summary>
		/// The input task ref id
		/// </summary>
		public RefId TaskRefId { get; }

		/// <summary>
		/// Timestamp for the event
		/// </summary>
		public DateTime Time { get; }

		/// <summary>
		/// State of the task
		/// </summary>
		public ComputeTaskState State { get; }

		/// <summary>
		/// Indicates the outcome of a task
		/// </summary>
		public ComputeTaskOutcome Outcome { get; }

		/// <summary>
		/// When transitioning to the executing state, includes the agent name
		/// </summary>
		public AgentId? AgentId { get; }

		/// <summary>
		/// The lease id
		/// </summary>
		public LeaseId? LeaseId { get; }

		/// <summary>
		/// Hash of the result, if complete
		/// </summary>
		public RefId? ResultRefId { get; }

		/// <summary>
		/// Additional information for the given outcome
		/// </summary>
		public string? Detail { get; }
	}

	/// <summary>
	/// Interface for the compute service
	/// </summary>
	public interface IComputeService : ITaskSource
	{
		/// <summary>
		/// Gets information about a compute cluster
		/// </summary>
		/// <param name="ClusterId">Cluster to use for execution</param>
		Task<IComputeClusterInfo> GetClusterInfoAsync(ClusterId ClusterId);

		/// <summary>
		/// Post tasks to be executed to a channel
		/// </summary>
		/// <param name="ClusterId">Cluster to use for execution</param>
		/// <param name="ChannelId">Unique identifier of the client</param>
		/// <param name="TaskRefIds">List of task hashes</param>
		/// <param name="RequirementsHash">Requirements document for execution</param>
		/// <returns>Async task</returns>
		Task AddTasksAsync(ClusterId ClusterId, ChannelId ChannelId, List<RefId> TaskRefIds, CbObjectAttachment RequirementsHash);

		/// <summary>
		/// Dequeue completed items from a queue and return immediately
		/// </summary>
		/// <param name="ClusterId">Cluster containing the channel</param>
		/// <param name="ChannelId">Queue to remove items from</param>
		/// <returns>List of status updates</returns>
		Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ClusterId ClusterId, ChannelId ChannelId);

		/// <summary>
		/// Dequeue completed items from a queue
		/// </summary>
		/// <param name="ClusterId">Cluster containing the channel</param>
		/// <param name="ChannelId">Queue to remove items from</param>
		/// <param name="CancellationToken">Cancellation token to stop waiting for items</param>
		/// <returns>List of status updates</returns>
		Task<List<IComputeTaskStatus>> WaitForTaskUpdatesAsync(ClusterId ClusterId, ChannelId ChannelId, CancellationToken CancellationToken);
	}
}
