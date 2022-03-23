// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Controllers
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
		readonly ILogger<SwarmController> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public SwarmController(ILogger<SwarmController> logger)
		{
			_logger = logger;
		}

		/// <summary>
		/// Gets the latest version info
		/// </summary>
		/// <returns>Result code</returns>
		[HttpPost]
		[Route("/api/v1/swarm/reviews")]
		public ActionResult AddReview(object request)
		{
			_logger.LogInformation("Added Swarm review: {Request}", request.ToString());
			return Ok();
		}
	}
}
