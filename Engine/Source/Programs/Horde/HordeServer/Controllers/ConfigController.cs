// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using System.Threading.Tasks;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using System.Runtime.InteropServices;

namespace HordeServer.Controllers
{

	/// <summary>
	/// Controller managing server configuration
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ConfigController : ControllerBase
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
		/// The config service singleton
		/// </summary>
		ConfigService ConfigService;
		/// <summary>
		/// Settings for the server
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		///  Logger for controller
		/// </summary>
		private readonly ILogger<ConfigController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="ConfigService">The Config service singleton</param>
		/// <param name="Logger">Logger for the controller</param>
		/// <param name="Settings">Server settings</param>
		public ConfigController(DatabaseService DatabaseService, AclService AclService, ConfigService ConfigService, IOptionsMonitor<ServerSettings> Settings, ILogger<ConfigController> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.AclService = AclService;
			this.ConfigService = ConfigService;
			this.Settings = Settings;
			this.Logger = Logger;			
		}

		// Global Configuration

		/// <summary>
		/// Update global config
		/// </summary>
		[HttpPut]
		[Route("/api/v1/config/global")]
		public async Task<ActionResult> UpdateGlobalSettings([FromBody] UpdateGlobalConfigRequest Request)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			if (!Settings.CurrentValue.SingleInstance)
			{
				Logger.LogError("Updating global configuration settings with ConfigController currently only supported in single instance mode");
				return BadRequest();
			}

			await ConfigService.UpdateGlobalConfig(Request);

			return Ok();
		}

		/// <summary>
		/// Get global config settings
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/config/global")]
		public async Task<ActionResult<GlobalConfig?>> GetGlobalConfig()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			if (string.IsNullOrEmpty(Settings.CurrentValue.ConfigPath))
			{
				return BadRequest();
			}

			FileReference GlobalConfigFile = new FileReference(Settings.CurrentValue.ConfigPath);

			byte[] Data = await FileReference.ReadAllBytesAsync(GlobalConfigFile);
			JsonSerializerOptions Options = new JsonSerializerOptions();
			Startup.ConfigureJsonSerializer(Options);

			return JsonSerializer.Deserialize<GlobalConfig>(Data, Options);
		}

		// Administrative Server Settings

		/// <summary>
		/// Get administrative server settings
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpGet]
		[Route("/api/v1/config/serversettings")]
		public async Task<ActionResult<GetServerSettingsResponse>> GetServerSettings()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			GetServerSettingsResponse Response = new GetServerSettingsResponse(Settings.CurrentValue);
			Response.NumServerUpdates = ConfigService.NumUserConfigUpdates;

			return Response;
		}

		/// <summary>
		/// Update administrative server settings
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpPut]
		[Route("/api/v1/config/serversettings")]
		public async Task<ActionResult<ServerUpdateResponse>> UpdateServerSettings([FromBody] UpdateServerSettingsRequest Request)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			if (!Settings.CurrentValue.SingleInstance)
			{
				Logger.LogError("Updating server settings from ConfigController currently only supported in single instance mode");
				return BadRequest();
			}

			if (Request.Settings == null || Request.Settings.Count == 0)
			{
				return new ServerUpdateResponse() { RestartRequired = false };
			}

			ServerUpdateResponse Response = await ConfigService.UpdateServerSettings(Request);

			return Response;
		}

	}

}
