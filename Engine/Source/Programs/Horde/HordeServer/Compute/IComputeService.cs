// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Compute;
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
	using ChannelId = StringId<IComputeChannel>;
	using LeaseId = ObjectId<ILease>;
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Unused, except for ChannelId alias.
	/// </summary>
	[SuppressMessage("Design", "CA1040:Avoid empty interfaces")]
	public interface IComputeChannel
	{
	}

	/// <summary>
	/// Status of a compute task
	/// </summary>
	public interface IComputeTaskStatus
	{
		/// <summary>
		/// The input hash
		/// </summary>
		public CbObjectAttachment Task { get; }

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
		public CbObjectAttachment? Result { get; }

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
		/// Post tasks to be executed to a channel
		/// </summary>
		/// <param name="NamespaceId">Namespace of referenced blobs</param>
		/// <param name="RequirementsHash">Hash of the requirements document for execution</param>
		/// <param name="TaskHashes">List of task hashes</param>
		/// <param name="ChannelId">Unique identifier of the client</param>
		/// <returns>Async task</returns>
		Task AddTasksAsync(NamespaceId NamespaceId, CbObjectAttachment RequirementsHash, List<CbObjectAttachment> TaskHashes, ChannelId ChannelId);

		/// <summary>
		/// Dequeue completed items from a queue and return immediately
		/// </summary>
		/// <param name="ChannelId">Queue to remove items from</param>
		/// <returns>List of status updates</returns>
		Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ChannelId ChannelId);

		/// <summary>
		/// Dequeue completed items from a queue
		/// </summary>
		/// <param name="ChannelId">Queue to remove items from</param>
		/// <param name="CancellationToken">Cancellation token to stop waiting for items</param>
		/// <returns>List of status updates</returns>
		Task<List<IComputeTaskStatus>> WaitForTaskUpdatesAsync(ChannelId ChannelId, CancellationToken CancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeService"/>
	/// </summary>
	static class ComputeServiceExtensions
	{
		/// <summary>
		/// 
		/// </summary>
		/// <param name="ComputeService"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="RequirementsHash">Hash of the requirements document for execution</param>
		/// <param name="TaskHash">The task hash</param>
		/// <param name="ChannelId"></param>
		/// <returns></returns>
		public static Task AddTaskAsync(this IComputeService ComputeService, NamespaceId NamespaceId, CbObjectAttachment RequirementsHash, CbObjectAttachment TaskHash, ChannelId ChannelId)
		{
			return ComputeService.AddTasksAsync(NamespaceId, RequirementsHash, new List<CbObjectAttachment> { TaskHash }, ChannelId);
		}
	}
}
