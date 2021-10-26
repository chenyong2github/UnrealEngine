// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Authorization.Infrastructure;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.IdentityModel.Tokens.Jwt;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Controllers
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
	public class AdminController : ControllerBase
	{
		/// <summary>
		/// The database service singleton
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// The acl service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// The upgrade service singleton
		/// </summary>
		UpgradeService UpgradeService;

		/// <summary>
		/// Settings for the server
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="UpgradeService">The upgrade service singelton</param>
		/// <param name="Settings">Server settings</param>
		public AdminController(DatabaseService DatabaseService, AclService AclService, UpgradeService UpgradeService, IOptionsMonitor<ServerSettings> Settings)
		{
			this.DatabaseService = DatabaseService;
			this.AclService = AclService;
			this.UpgradeService = UpgradeService;
			this.Settings = Settings;
		}

		/// <summary>
		/// Force a reset on the database
		/// </summary>
		[HttpPost]
		[Route("/api/v1/admin/reset")]
		public async Task<ActionResult> ForceResetAsync([FromQuery] string? Instance = null)
		{
			if(!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			for (; ; )
			{
				Globals Globals = await DatabaseService.GetGlobalsAsync();
				if (Instance == null)
				{
					return NotFound($"Missing code query parameter. Set to {Globals.InstanceId} to reset.");
				}
				if (Globals.InstanceId != Instance.ToObjectId())
				{
					return NotFound($"Incorrect code query parameter. Should be {Globals.InstanceId}.");
				}

				Globals.ForceReset = true;

				if (await DatabaseService.TryUpdateSingletonAsync(Globals))
				{
					return Ok("Database will be reinitialized on next restart");
				}
			}
		}

		/// <summary>
		/// Upgrade the database to the latest schema
		/// </summary>
		/// <param name="FromVersion">The schema version to upgrade from.</param>
		[HttpPost]
		[Route("/api/v1/admin/upgradeschema")]
		public async Task<ActionResult> UpgradeSchemaAsync([FromQuery] int? FromVersion = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			await UpgradeService.UpgradeSchemaAsync(FromVersion);
			return Ok();
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/token")]
		public async Task<ActionResult<string>> GetTokenAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.IssueBearerToken, User))
			{
				return Forbid();
			}

			return AclService.IssueBearerToken(User.Claims, GetDefaultExpiryTime());
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <param name="Roles">Roles for the new token</param>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/roletoken")]
		public async Task<ActionResult<string>> GetRoleTokenAsync([FromQuery] string Roles)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			List<Claim> Claims = new List<Claim>();
			Claims.AddRange(Roles.Split('+').Select(x => new Claim(ClaimTypes.Role, x)));

			return AclService.IssueBearerToken(Claims, GetDefaultExpiryTime());
		}

		/// <summary>
		/// Issues a token for the given roles. Issues a token for the current user if not specified.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/registrationtoken")]
		public async Task<ActionResult<string>> GetRegistrationTokenAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			List<AclClaim> Claims = new List<AclClaim>();
			Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			Claims.Add(AclService.AgentRegistrationClaim);

			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Issues a token valid to upload new versions of the agent software.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/softwaretoken")]
		public async Task<ActionResult<string>> GetSoftwareTokenAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			List<AclClaim> Claims = new List<AclClaim>();
			Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			Claims.Add(AclService.UploadSoftwareClaim);

			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Issues a token valid to download new versions of the agent software.
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/softwaredownloadtoken")]
		public async Task<ActionResult<string>> GetSoftwareDownloadTokenAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<AclClaim> Claims = new List<AclClaim>();
			Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			Claims.Add(AclService.DownloadSoftwareClaim);

			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Issues a token valid to configure streams and projects
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/configtoken")]
		public async Task<ActionResult<string>> GetConfigToken()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<AclClaim> Claims = new List<AclClaim>();
			Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity?.Name ?? "Unknown"));
			Claims.Add(AclService.ConfigureProjectsClaim);

			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Issues a token valid to start chained jobs
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/admin/chainedjobtoken")]
		public async Task<ActionResult<string>> GetChainedJobToken()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<AclClaim> Claims = new List<AclClaim>();
			//Claims.Add(new AclClaim(ClaimTypes.Name, User.Identity.Name ?? "Unknown"));
			Claims.Add(AclService.StartChainedJobClaim);

			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Gets the default expiry time for a token
		/// </summary>
		/// <returns></returns>
		private TimeSpan? GetDefaultExpiryTime()
		{
			TimeSpan? ExpiryTime = null;
			if (Settings.CurrentValue.JwtExpiryTimeHours != -1)
			{
				ExpiryTime = TimeSpan.FromHours(Settings.CurrentValue.JwtExpiryTimeHours);
			}
			return ExpiryTime;
		}
	}
}
