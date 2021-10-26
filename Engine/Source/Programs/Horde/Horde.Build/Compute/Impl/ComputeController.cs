// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Compute;
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
	using ChannelId = StringId<IComputeChannel>;

	/// <summary>
	/// Controller for the /api/v1/compute endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ComputeController : ControllerBase
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
		/// Add tasks to be executed remotely
		/// </summary>
		/// <param name="ChannelId">The channel to add to</param>
		/// <param name="Request">The request parameters</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/compute/{ChannelId}")]
		public async Task<ActionResult> AddTasksAsync([FromRoute] ChannelId ChannelId, [FromBody] AddTasksRequest Request)
		{
			if (Request.TaskHashes.Count == 0)
			{
				return BadRequest("No task hashes specified");
			}

			await ComputeService.AddTasksAsync(Impl.ComputeService.DefaultNamespaceId, Request.RequirementsHash, Request.TaskHashes, ChannelId);
			return Ok();
		}

		/// <summary>
		/// Read updates for a particular channel
		/// </summary>
		/// <param name="ChannelId">The channel to add to</param>
		/// <param name="Wait">Amount of time to wait before responding, in seconds</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/compute/{ChannelId}/updates")]
		public async Task<GetTaskUpdatesResponse> GetUpdatesAsync([FromRoute] ChannelId ChannelId, [FromQuery] int Wait = 0)
		{
			List<IComputeTaskStatus> Results;
			if (Wait == 0)
			{
				Results = await ComputeService.GetTaskUpdatesAsync(ChannelId);
			}
			else
			{
				using CancellationTokenSource DelaySource = new CancellationTokenSource(Wait * 1000);
				Results = await ComputeService.WaitForTaskUpdatesAsync(ChannelId, DelaySource.Token);
			}

			GetTaskUpdatesResponse Response = new GetTaskUpdatesResponse();
			foreach (IComputeTaskStatus Result in Results)
			{
				GetTaskUpdateResponse Update = new GetTaskUpdateResponse();

				Update.TaskHash = Result.Task;
				Update.Time = Result.Time;
				Update.State = Result.State;
				Update.Outcome = Result.Outcome;
				Update.Result = Result.Result;
				Update.AgentId = Result.AgentId?.ToString();
				Update.LeaseId = Result.LeaseId?.ToString();
				Update.Detail = Result.Detail;

				Response.Updates.Add(Update);
			}

			return Response;
		}
	}
}
