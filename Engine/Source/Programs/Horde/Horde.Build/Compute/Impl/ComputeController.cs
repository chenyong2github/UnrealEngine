// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Impl;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Compute.Impl
{
	/// <summary>
	/// Controller for the /api/v1/compute endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ComputeController : HordeControllerBase
	{
		AclService AclService;
		IComputeService ComputeService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService"></param>
		/// <param name="ComputeService">The compute service singleton</param>
		public ComputeController(AclService AclService, IComputeService ComputeService)
		{
			this.AclService = AclService;
			this.ComputeService = ComputeService;
		}

		/// <summary>
		/// Gets information about a cluster
		/// </summary>
		/// <param name="ClusterId">The cluster to add to</param>
		/// <returns></returns>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/compute/{ClusterId}")]
		public async Task<ActionResult<GetComputeClusterInfo>> GetClusterInfoAsync([FromRoute] ClusterId ClusterId)
		{
			if (!await AclService.AuthorizeAsync(AclAction.ViewComputeTasks, User))
			{
				return Forbid(AclAction.ViewComputeTasks);
			}

			IComputeClusterInfo ClusterInfo;
			try
			{
				ClusterInfo = await ComputeService.GetClusterInfoAsync(ClusterId);
			}
			catch (KeyNotFoundException)
			{
				return NotFound("Cluster '{ClusterId}' was found", ClusterId);
			}

			GetComputeClusterInfo Response = new GetComputeClusterInfo();
			Response.Id = ClusterInfo.Id;
			Response.NamespaceId = ClusterInfo.NamespaceId;
			Response.RequestBucketId = ClusterInfo.RequestBucketId;
			Response.ResponseBucketId = ClusterInfo.ResponseBucketId;

			return Response;
		}

		/// <summary>
		/// Add tasks to be executed remotely
		/// </summary>
		/// <param name="ClusterId">The cluster to add to</param>
		/// <param name="Request">The request parameters</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/compute/{ClusterId}")]
		public async Task<ActionResult> AddTasksAsync([FromRoute] ClusterId ClusterId, [FromBody] AddTasksRequest Request)
		{
			if(!await AclService.AuthorizeAsync(AclAction.AddComputeTasks, User))
			{
				return Forbid(AclAction.AddComputeTasks);
			}
			if (Request.TaskRefIds.Count == 0)
			{
				return BadRequest("No task hashes specified");
			}

			await ComputeService.AddTasksAsync(ClusterId, Request.ChannelId, Request.TaskRefIds, Request.RequirementsHash);
			return Ok();
		}

		/// <summary>
		/// Read updates for a particular channel
		/// </summary>
		/// <param name="ClusterId"></param>
		/// <param name="ChannelId">The channel to add to</param>
		/// <param name="Wait">Amount of time to wait before responding, in seconds</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/compute/{ClusterId}/updates/{ChannelId}")]
		public async Task<ActionResult<GetTaskUpdatesResponse>> GetUpdatesAsync([FromRoute] ClusterId ClusterId, [FromRoute] ChannelId ChannelId, [FromQuery] int Wait = 0)
		{
			if (!await AclService.AuthorizeAsync(AclAction.ViewComputeTasks, User))
			{
				return Forbid(AclAction.ViewComputeTasks);
			}

			List<IComputeTaskStatus> Results;
			if (Wait == 0)
			{
				Results = await ComputeService.GetTaskUpdatesAsync(ClusterId, ChannelId);
			}
			else
			{
				using CancellationTokenSource DelaySource = new CancellationTokenSource(Wait * 1000);
				Results = await ComputeService.WaitForTaskUpdatesAsync(ClusterId, ChannelId, DelaySource.Token);
			}

			GetTaskUpdatesResponse Response = new GetTaskUpdatesResponse();
			foreach (IComputeTaskStatus Result in Results)
			{
				GetTaskUpdateResponse Update = new GetTaskUpdateResponse();

				Update.TaskRefId = Result.TaskRefId;
				Update.Time = Result.Time;
				Update.State = Result.State;
				Update.Outcome = Result.Outcome;
				Update.ResultRefId = Result.ResultRefId;
				Update.AgentId = Result.AgentId?.ToString();
				Update.LeaseId = Result.LeaseId?.ToString();
				Update.Detail = Result.Detail;

				Response.Updates.Add(Update);
			}

			return Response;
		}
	}
}
