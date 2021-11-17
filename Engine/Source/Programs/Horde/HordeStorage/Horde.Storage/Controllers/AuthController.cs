// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;
using Datadog.Trace;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/auth")]
    public class AuthController : ControllerBase
    {
        private readonly IAuthorizationService _authorizationService;

        public AuthController(IAuthorizationService authorizationService)
        {
            _authorizationService = authorizationService;
        }

        [HttpGet("{ns}")]
        [Authorize("Any")]
        public async Task<IActionResult> Verify(
            [FromRoute] [Required] NamespaceId ns
            )
        {
            AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

            if (!authorizationResult.Succeeded)
            {
                return Forbid();
            }

            return Ok();
        }
    }
}
