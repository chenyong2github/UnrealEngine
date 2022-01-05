// Copyright Epic Games, Inc. All Rights Reserved.

// Copyright Epic Games, Inc. All Rights Reserved.	

using HordeServer.Authentication;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Controllers;
using Microsoft.Extensions.Options;
using System;
using System.Text;

namespace Horde.Build
{
	/// <summary>	
	/// Dashboard authorization challenge controller	
	/// </summary>	
	[ApiController]
	[Route("[controller]")]
	public class DashboardChallengeController : Controller
	{
		/// <summary>
		/// Authentication scheme in use
		/// </summary>
		string AuthenticationScheme;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerSettings">Server settings</param>
		public DashboardChallengeController(IOptionsMonitor<ServerSettings> ServerSettings)
		{
			AuthenticationScheme = AccountController.GetAuthScheme(ServerSettings.CurrentValue.AuthMethod);
		}

		/// <summary>	
		/// Challenge endpoint for the dashboard, using cookie authentication scheme	
		/// </summary>	
		/// <returns>Ok on authorized, otherwise will 401</returns>	
		[HttpGet]
		[Authorize]
		[Route("/api/v1/dashboard/challenge")]
		public StatusCodeResult GetChallenge()
		{
			return Ok();
		}

		/// <summary>
		/// Login to server, redirecting to the specified URL on success
		/// </summary>
		/// <param name="Redirect"></param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/dashboard/login")]
		public IActionResult Login([FromQuery] string? Redirect)
		{
			return new ChallengeResult(AuthenticationScheme, new AuthenticationProperties { RedirectUri = Redirect ?? "/" });
		}

		/// <summary>
		/// Login to server, redirecting to the specified Base64 encoded URL, which fixes some escaping issues on some auth providers, on success
		/// </summary>
		/// <param name="Redirect"></param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v2/dashboard/login")]
		public IActionResult LoginV2([FromQuery] string? Redirect)
		{
			string? RedirectUri = null;

			if (Redirect != null)
			{
				byte[] Data = Convert.FromBase64String(Redirect);
				RedirectUri = Encoding.UTF8.GetString(Data);
			}

			return new ChallengeResult(AuthenticationScheme, new AuthenticationProperties { RedirectUri = RedirectUri ?? "/index" });
		}


		/// <summary>
		/// Logout of the current account
		/// </summary>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/dashboard/logout")]
		public async Task<StatusCodeResult> Logout()
		{
			await HttpContext.SignOutAsync(CookieAuthenticationDefaults.AuthenticationScheme);
			try
			{
				await HttpContext.SignOutAsync(AuthenticationScheme);
			}
#pragma warning disable CA1031 // Do not catch general exception types
			catch
#pragma warning restore CA1031
			{
			}

			return Ok();
		}


	}
}
