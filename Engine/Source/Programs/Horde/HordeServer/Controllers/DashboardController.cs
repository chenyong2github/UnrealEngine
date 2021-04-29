// Copyright Epic Games, Inc. All Rights Reserved.

// Copyright Epic Games, Inc. All Rights Reserved.	

using HordeServer.Authentication;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System.Threading.Tasks;

/// <summary>	
/// Dashboard authorization challenge controller	
/// </summary>	
[ApiController]
[Route("[controller]")]
public class DashboardChallengeController : Controller
{
	const string DefaultAuthenticationScheme = OktaDefaults.AuthenticationScheme;

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
		return new ChallengeResult(DefaultAuthenticationScheme, new AuthenticationProperties { RedirectUri = Redirect ?? "/" });
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
			await HttpContext.SignOutAsync(DefaultAuthenticationScheme);
		}
#pragma warning disable CA1031 // Do not catch general exception types
		catch
#pragma warning restore CA1031 
		{
		}

		return Ok();
	}


}