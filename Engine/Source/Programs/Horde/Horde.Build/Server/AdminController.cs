// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Build.Server
{
	/// <summary>
	/// Object containing settings for the server
	/// </summary>
	public class AdminSettings
	{
		/// <summary>
		/// The default perforce server
		/// </summary>
		public string? DefaultServerAndPort { get; set; }

		/// <summary>
		/// The default perforce username
		/// </summary>
		public string? DefaultUserName { get; set; }

		/// <summary>
		/// The default perforce password
		/// </summary>
		public string? DefaultPassword { get; set; }
	}

	/// <summary>
	/// The conform limit value
	/// </summary>
	public class ConformSettings
	{
		/// <summary>
		/// Maximum number of conforms allowed at once
		/// </summary>
		public int MaxCount { get; set; }
	}

	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class AdminController : HordeControllerBase
	{
		readonly AclService _aclService;
		readonly IOptionsMonitor<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="aclService">The ACL service singleton</param>
		/// <param name="settings">Server settings</param>
		public AdminController(AclService aclService, IOptionsMonitor<ServerSettings> settings)
		{
			_aclService = aclService;
			_settings = settings;
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/token")]
		public async Task<ActionResult<string>> GetTokenAsync()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.IssueBearerToken, User))
			{
				return Forbid(AclAction.IssueBearerToken);
			}

			return await _aclService.IssueBearerTokenAsync(User.Claims, GetDefaultExpiryTime());
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <param name="roles">Roles for the new token</param>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/roletoken")]
		public async Task<ActionResult<string>> GetRoleTokenAsync([FromQuery] string roles)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid(AclAction.AdminWrite);
			}

			List<Claim> claims = new List<Claim>();
			claims.AddRange(roles.Split('+').Select(x => new Claim(ClaimTypes.Role, x)));

			return await _aclService.IssueBearerTokenAsync(claims, GetDefaultExpiryTime());
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/registrationtoken")]
		public async Task<ActionResult<string>> GetRegistrationTokenAsync()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid(AclAction.AdminWrite);
			}

			List<AclClaim> claims = new List<AclClaim>();
			claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			claims.Add(AclService.AgentRegistrationClaim);

			return await _aclService.IssueBearerTokenAsync(claims, null);
		}

		/// <summary>
		/// Issues a token valid to upload new versions of the agent software.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/softwaretoken")]
		public async Task<ActionResult<string>> GetSoftwareTokenAsync()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid(AclAction.AdminWrite);
			}

			List<AclClaim> claims = new List<AclClaim>();
			claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			claims.Add(AclService.UploadSoftwareClaim);

			return await _aclService.IssueBearerTokenAsync(claims, null);
		}

		/// <summary>
		/// Issues a token valid to download new versions of the agent software.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/softwaredownloadtoken")]
		public async Task<ActionResult<string>> GetSoftwareDownloadTokenAsync()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid(AclAction.AdminRead);
			}

			List<AclClaim> claims = new List<AclClaim>();
			claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			claims.Add(AclService.DownloadSoftwareClaim);

			return await _aclService.IssueBearerTokenAsync(claims, null);
		}

		/// <summary>
		/// Issues a token valid to configure streams and projects
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/configtoken")]
		public async Task<ActionResult<string>> GetConfigToken()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid(AclAction.AdminRead);
			}

			List<AclClaim> claims = new List<AclClaim>();
			claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			claims.Add(AclService.ConfigureProjectsClaim);

			return await _aclService.IssueBearerTokenAsync(claims, null);
		}

		/// <summary>
		/// Issues a token valid to start chained jobs
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/chainedjobtoken")]
		public async Task<ActionResult<string>> GetChainedJobToken()
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid(AclAction.AdminRead);
			}

			List<AclClaim> claims = new List<AclClaim>();
			//Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity.Name ?? "Unknown"));
			claims.Add(AclService.StartChainedJobClaim);

			return await _aclService.IssueBearerTokenAsync(claims, null);
		}

		/// <summary>
		/// Gets the default expiry time for a token
		/// </summary>
		/// <returns></returns>
		private TimeSpan? GetDefaultExpiryTime()
		{
			TimeSpan? expiryTime = null;
			if (_settings.CurrentValue.JwtExpiryTimeHours != -1)
			{
				expiryTime = TimeSpan.FromHours(_settings.CurrentValue.JwtExpiryTimeHours);
			}
			return expiryTime;
		}
	}
}
