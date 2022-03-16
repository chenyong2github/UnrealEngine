// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace Horde.Build.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/projects endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class SchedulesController : ControllerBase
	{
		/// <summary>
		/// Query all the schedules
		/// </summary>
		/// <param name="StreamId">Id of the stream to search for schedules for</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the schedules</returns>
		[HttpGet]
		[Route("/api/v1/schedules")]
		[ProducesResponseType(typeof(List<GetScheduleResponse>), 200)]
#pragma warning disable CA1801 // Remove unused parameter
		public Task<ActionResult<List<object>>> GetSchedulesAsync([FromQuery] string? StreamId = null, [FromQuery] PropertyFilter? Filter = null)
		{
			return Task.FromResult<ActionResult<List<object>>>(new List<object>());
		}
#pragma warning restore CA1801 // Remove unused parameter
	}
}
