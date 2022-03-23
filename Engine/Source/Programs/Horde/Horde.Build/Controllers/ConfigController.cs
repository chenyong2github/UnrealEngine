// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Build.Controllers
{

	/// <summary>
	/// Controller managing server configuration
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ConfigController : HordeControllerBase
	{
		/// <summary>
		/// The acl service singleton
		/// </summary>
		readonly AclService _aclService;

		/// <summary>
		/// The config service singleton
		/// </summary>
		readonly ConfigService _configService;

		/// <summary>
		/// Settings for the server
		/// </summary>
		readonly IOptionsMonitor<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="aclService">The ACL service singleton</param>
		/// <param name="configService">The Config service singleton</param>
		/// <param name="settings">Server settings</param>
		public ConfigController(AclService aclService, ConfigService configService, IOptionsMonitor<ServerSettings> settings)
		{
			_aclService = aclService;
			_configService = configService;
			_settings = settings;
		}

		// Global Configuration

		/// <summary>
		/// Update global config
		/// </summary>
		[HttpPut]
		[Route("/api/v1/config/global")]
		public async Task<ActionResult> UpdateGlobalSettings([FromBody] UpdateGlobalConfigRequest request)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid(AclAction.AdminWrite);
			}

			if (!_settings.CurrentValue.SingleInstance)
			{
				return BadRequest("Updating global configuration settings with ConfigController currently only supported in single instance mode");
			}

			await _configService.UpdateGlobalConfig(request);

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
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid(AclAction.AdminRead);
			}

			if (String.IsNullOrEmpty(_settings.CurrentValue.ConfigPath))
			{
				return BadRequest("Missing config path for settings");
			}

			FileReference globalConfigFile = new FileReference(_settings.CurrentValue.ConfigPath);

			byte[] data = await FileReference.ReadAllBytesAsync(globalConfigFile);
			JsonSerializerOptions options = new JsonSerializerOptions();
			Startup.ConfigureJsonSerializer(options);

			return JsonSerializer.Deserialize<GlobalConfig>(data, options);
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
			if (!await _aclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid(AclAction.AdminRead);
			}

			GetServerSettingsResponse response = new GetServerSettingsResponse(_settings.CurrentValue);
			response.NumServerUpdates = _configService.NumUserConfigUpdates;

			return response;
		}

		/// <summary>
		/// Update administrative server settings
		/// </summary>
		/// <returns>Administrative settings for the server</returns>
		[HttpPut]
		[Route("/api/v1/config/serversettings")]
		public async Task<ActionResult<ServerUpdateResponse>> UpdateServerSettings([FromBody] UpdateServerSettingsRequest request)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid(AclAction.AdminWrite);
			}

			if (!_settings.CurrentValue.SingleInstance)
			{
				return BadRequest("Updating server settings from ConfigController currently only supported in single instance mode");
			}

			if (request.Settings == null || request.Settings.Count == 0)
			{
				return new ServerUpdateResponse() { RestartRequired = false };
			}

			ServerUpdateResponse response = await _configService.UpdateServerSettings(request);

			return response;
		}
	}
}
