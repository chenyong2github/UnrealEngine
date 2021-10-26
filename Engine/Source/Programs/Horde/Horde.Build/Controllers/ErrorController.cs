// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Diagnostics;
using Microsoft.AspNetCore.Mvc;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller which returns exception metadata to the caller
	/// </summary>
	[ApiController]
	[ApiExplorerSettings(IgnoreApi = true)]
	public class ErrorController : ControllerBase
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

			return Problem(detail: Context.Error.StackTrace, title: Context.Error.Message);
		}
	}
}
