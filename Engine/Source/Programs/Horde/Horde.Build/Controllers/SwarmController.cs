// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Runtime.Internal.Util;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/swarm endpoint
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public sealed class SwarmController : ControllerBase
	{
		/// <summary>
		/// Logger instance
		/// </summary>
		ILogger<SwarmController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public SwarmController(ILogger<SwarmController> Logger)
		{
			this.Logger = Logger;
		}

		/// <summary>
		/// Gets the latest version info
		/// </summary>
		/// <returns>Result code</returns>
		[HttpPost]
		[Route("/api/v1/swarm/reviews")]
		public ActionResult AddReview(object Request)
		{
			Logger.LogInformation("Added Swarm review: {Request}", Request.ToString());
			return Ok();
		}
	}
}
