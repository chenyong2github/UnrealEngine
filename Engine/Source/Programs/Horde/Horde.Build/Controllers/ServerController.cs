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
using System.Reflection;
using System.Diagnostics;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ServerController : ControllerBase
	{

		/// <summary>
		/// Settings for the server
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Constructor
		/// </summary>
		public ServerController(IOptionsMonitor<ServerSettings> Settings)
		{
			this.Settings = Settings;
		}
		
		/// <summary>
		/// Get server version
		/// </summary>
		[HttpGet]
		[Route("/api/v1/server/version")]
		public ActionResult GetVersionAsync()
		{
			FileVersionInfo FileVersionInfo = FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location);
			return Ok(FileVersionInfo.ProductVersion);
		}		

		/// <summary>
		/// Get server information
		/// </summary>
		[HttpGet]
		[Route("/api/v1/server/info")]
		[ProducesResponseType(typeof(GetServerInfoResponse), 200)]
		public ActionResult<GetServerInfoResponse> GetServerInfo()
		{
			return new GetServerInfoResponse(this.Settings.CurrentValue.SingleInstance);
		}
	}
}
