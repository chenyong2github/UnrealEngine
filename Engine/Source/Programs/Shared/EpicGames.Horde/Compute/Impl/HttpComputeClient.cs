// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute.Impl
{
	#region Requests / Responses

	/// <summary>
	/// Information about a compute cluster
	/// </summary>
	public class GetComputeClusterInfo : IComputeClusterInfo
	{
		/// <inheritdoc/>
		public ClusterId Id { get; set; }

		/// <inheritdoc/>
		public NamespaceId NamespaceId { get; set; }

		/// <inheritdoc/>
		public BucketId RequestBucketId { get; set; }

		/// <inheritdoc/>
		public BucketId ResponseBucketId { get; set; }
	}

	/// <summary>
	/// Supplies information about the current execution state of a task
	/// </summary>
	public class GetTaskUpdateResponse : IComputeTaskInfo
	{
		/// <inheritdoc/>
		[CbField("h")]
		public RefId TaskRefId { get; set; }

		/// <inheritdoc/>
		[CbField("t")]
		public DateTime Time { get; set; }

		/// <inheritdoc/>
		[CbField("s")]
		public ComputeTaskState State { get; set; }

		/// <inheritdoc/>
		[CbField("o")]
		public ComputeTaskOutcome Outcome { get; set; }

		/// <inheritdoc/>
		[CbField("d")]
		public string? Detail { get; set; }

		/// <inheritdoc/>
		[CbField("r")]
		public RefId? ResultRefId { get; set; }

		/// <inheritdoc/>
		[CbField("a")]
		public string? AgentId { get; set; }

		/// <inheritdoc/>
		[CbField("l")]
		public string? LeaseId { get; set; }
	}

	/// <summary>
	/// Request to add tasks to the compute queue
	/// </summary>
	public class AddTasksRequest
	{
		/// <summary>
		/// Channel to post the new tasks to. This should be a unique identifier (eg. a GUID) synthesized by the client to distinguish it from other clients, and used for querying status later.
		/// </summary>
		[CbField("c")]
		public ChannelId ChannelId { get; set; }

		/// <summary>
		/// Refs for tasks to be executed
		/// </summary>
		[Required, CbField("t")]
		public List<RefId> TaskRefIds { get; set; } = new List<RefId>();

		/// <summary>
		/// Requirements for agents executing the tasks
		/// </summary>
		[CbField("r")]
		public CbObjectAttachment RequirementsHash { get; set; }

		/// <summary>
		/// If set, prevents data being cached.
		/// </summary>
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

	#endregion

	/// <summary>
	/// Implementation of <see cref="IComputeClient"/> which uses HTTP to communicate with a Horde Storage server
	/// </summary>
	public class HttpComputeClient : IComputeClient
	{
		HttpClient HttpClient;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="HttpClient"></param>
		public HttpComputeClient(HttpClient HttpClient)
		{
			this.HttpClient = HttpClient;
		}

		/// <inheritdoc/>
		public async Task<IComputeClusterInfo> GetClusterInfoAsync(ClusterId ClusterId, CancellationToken CancellationToken)
		{
			return await HttpClient.GetAsync<GetComputeClusterInfo>($"api/v1/compute/{ClusterId}", CancellationToken);
		}

		/// <inheritdoc/>
		public async Task AddTasksAsync(ClusterId ClusterId, ChannelId ChannelId, IEnumerable<RefId> TaskRefIds, IoHash RequirementsHash, bool SkipCacheLookup, CancellationToken CancellationToken)
		{
			AddTasksRequest AddTasks = new AddTasksRequest();
			AddTasks.ChannelId = ChannelId;
			AddTasks.TaskRefIds.AddRange(TaskRefIds);
			AddTasks.RequirementsHash = RequirementsHash;
			AddTasks.DoNotCache = SkipCacheLookup;

			ReadOnlyMemoryContent Content = new ReadOnlyMemoryContent(CbSerializer.Serialize(AddTasks).GetView());
			Content.Headers.Add("Content-Type", "application/x-ue-cb");

			HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/compute/{ClusterId}");
			Request.Content = Content;

			HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			Response.EnsureSuccessStatusCode();
		}

		/// <inheritdoc/>
		public async IAsyncEnumerable<IComputeTaskInfo> GetTaskUpdatesAsync(ClusterId ClusterId, ChannelId ChannelId, [EnumeratorCancellation] CancellationToken CancellationToken)
		{
			for (; ; )
			{
				HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/compute/{ClusterId}/updates/{ChannelId}?wait=10");
				Request.Headers.Add("Accept", "application/x-ue-cb");

				using HttpResponseMessage Response = await HttpClient.SendAsync(Request, HttpCompletionOption.ResponseContentRead, CancellationToken);
				Response.EnsureSuccessStatusCode();

				byte[] Data = await Response.Content.ReadAsByteArrayAsync();
				GetTaskUpdatesResponse ParsedResponse = CbSerializer.Deserialize<GetTaskUpdatesResponse>(new CbField(Data));

				foreach (GetTaskUpdateResponse Update in ParsedResponse.Updates)
				{
					CancellationToken.ThrowIfCancellationRequested();
					yield return Update;
				}
			}
		}
	}
}
