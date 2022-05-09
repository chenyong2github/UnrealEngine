// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc;
using System.ComponentModel.DataAnnotations;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Authorization;
using EpicGames.Horde.Storage;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/auth")]
    public class AuthController : ControllerBase
    {
        private readonly RequestHelper _requestHelper;

        public AuthController(RequestHelper requestHelper)
        {
            _requestHelper = requestHelper;
        }

        [HttpGet("{ns}")]
        [Authorize("Any")]
        public async Task<IActionResult> Verify(
            [FromRoute] [Required] NamespaceId ns
            )
        {
            ActionResult? result = await _requestHelper.HasAccessToNamespace(User, Request, ns);
            if (result != null)
            {
                return result;
            }

            return Ok();
        }
    }
}
