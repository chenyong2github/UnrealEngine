// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Diagnostics;
using Microsoft.AspNetCore.Mvc;

namespace Jupiter.Controllers
{
    [ApiController]
    [ApiExplorerSettings(IgnoreApi = true)]
    public class ErrorController : ControllerBase
    {
        [Route("/error")]
        public IActionResult Error()
        {
            var context = HttpContext.Features.Get<IExceptionHandlerFeature>();

            if (context == null)
                return NoContent();

            return Problem(
                detail: context.Error.StackTrace,
                title: context.Error.Message);
        }
    }
}
