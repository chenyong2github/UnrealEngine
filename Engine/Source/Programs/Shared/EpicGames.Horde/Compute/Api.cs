// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Request to add tasks to the compute queue
	/// </summary>
	public class AddTasksRequest
	{
		/// <inheritdoc cref="AddTasksRpcRequest.RequirementsHash"/>
		[Required]
		public string RequirementsHash { get; set; } = String.Empty;

		/// <inheritdoc cref="AddTasksRpcRequest.TaskHashes"/>
		[Required]
		public List<string> TaskHashes { get; set; } = new List<string>();

		/// <inheritdoc cref="AddTasksRpcRequest.DoNotCache"/>
		public bool DoNotCache { get; set; }
	}

	/// <summary>
	/// Specifies updates from the given channel
	/// </summary>
	public class GetTaskUpdatesResponse
	{
		/// <summary>
		/// Task updates
		/// </summary>
		public List<GetTaskUpdateResponse> Updates { get; set; } = new List<GetTaskUpdateResponse>();
	}

	/// <summary>
	/// Supplies information about the current execution state of a task
	/// </summary>
	public class GetTaskUpdateResponse
	{
		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.TaskHash"/>
		public string TaskHash { get; set; } = null!;

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.Time"/>
		public DateTime Time { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.State"/>
		public ComputeTaskState State { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.ResultHash"/>
		public string? ResultHash { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.AgentId"/>
		public string? AgentId { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.LeaseId"/>
		public string? LeaseId { get; set; }
	}

	partial class AddTasksRpcRequest
	{
		public AddTasksRpcRequest(string ChannelId, string NamespaceId, IoHash RequirementsHash, List<IoHash> TaskHashes, bool DoNotCache)
		{
			this.ChannelId = ChannelId;
			this.NamespaceId = NamespaceId;
			this.RequirementsHash = RequirementsHash;
			this.TaskHashes.Add(TaskHashes.Select(x => (IoHashWrapper)x));
			this.DoNotCache = DoNotCache;
		}
	}

	partial class GetTaskUpdatesRpcRequest
	{
		public GetTaskUpdatesRpcRequest(string ChannelId)
		{
			this.ChannelId = ChannelId;
		}
	}
}
