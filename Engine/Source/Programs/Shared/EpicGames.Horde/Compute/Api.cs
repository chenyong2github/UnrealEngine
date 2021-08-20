// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Serialization;
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
		[Required, CbField("r")]
		public IoHash RequirementsHash { get; set; }

		/// <inheritdoc cref="AddTasksRpcRequest.TaskHashes"/>
		[Required, CbField("t")]
		public List<IoHash> TaskHashes { get; set; } = new List<IoHash>();

		/// <inheritdoc cref="AddTasksRpcRequest.DoNotCache"/>
		[CbField("nc")]
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
		[CbField("u")]
		public List<GetTaskUpdateResponse> Updates { get; set; } = new List<GetTaskUpdateResponse>();
	}

	/// <summary>
	/// Supplies information about the current execution state of a task
	/// </summary>
	public class GetTaskUpdateResponse
	{
		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.TaskHash"/>
		[CbField("h")]
		public IoHash TaskHash { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.Time"/>
		[CbField("t")]
		public DateTime Time { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.State"/>
		[CbField("s")]
		public ComputeTaskState State { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.ResultHash"/>
		[CbField("r")]
		public IoHash? ResultHash { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.AgentId"/>
		[CbField("a")]
		public string? AgentId { get; set; }

		/// <inheritdoc cref="GetTaskUpdatesRpcResponse.LeaseId"/>
		[CbField("l")]
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
