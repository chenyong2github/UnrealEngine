// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Diagnostics;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller which returns exception metadata to the caller
	/// </summary>
	[ApiController]
	[ApiExplorerSettings(IgnoreApi = true)]
	public class ErrorController : HordeControllerBase
	{
		/// <summary>
		/// Handle an exception and generate a problem response
		/// </summary>
		/// <returns></returns>
		[Route("/api/v1/error")]
		public IActionResult Error()
		{
			IExceptionHandlerFeature? Context = HttpContext.Features.Get<IExceptionHandlerFeature>();
			if (Context == null)
			{
				return NoContent();
			}
			return StatusCode(StatusCodes.Status500InternalServerError, LogEvent.Create(LogLevel.Error, KnownLogEvents.None, Context.Error, Context.Error.Message));
		}
	}
}
