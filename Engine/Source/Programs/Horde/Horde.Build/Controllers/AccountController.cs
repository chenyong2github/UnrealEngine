// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Authentication;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authentication.OpenIdConnect;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class AccountController : Controller
	{
		const string DefaultAuthenticationScheme = OktaDefaults.AuthenticationScheme;

		/// <summary>
		/// Style sheet for HTML responses
		/// </summary>
		const string StyleSheet =
			"body { font-family: 'Segoe UI', 'Roboto', arial, sans-serif; } " +
			"p { margin:20px; font-size:13px; } " +
			"h1 { margin:20px; font-size:32px; font-weight:200; } " +
			"table { margin:10px 20px; } " +
			"td { margin:5px; font-size:13px; }";

		/// <summary>
		/// The ACL service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">ACL service instance</param>
		public AccountController(AclService AclService)
		{
			this.AclService = AclService;
		}

		/// <summary>
		/// Gets the current login status
		/// </summary>
		/// <returns>The current login state</returns>
		[HttpGet]
		[Route("/account")]
		public async Task<ActionResult> State()
		{
			StringBuilder Content = new StringBuilder();
			Content.Append($"<html><style>{StyleSheet}</style><h1>Horde Server</h1>");
			if (User.Identity?.IsAuthenticated ?? false)
			{
				Content.Append(CultureInfo.InvariantCulture, $"<p>User <b>{User.Identity?.Name}</b> is logged in. <a href=\"/account/logout\">Log out</a></p>");
				if (await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
				{
					Content.Append("<p>");
					Content.Append("<a href=\"/api/v1/admin/token\">Get bearer token</a><br/>");
					Content.Append("<a href=\"/api/v1/admin/registrationtoken\">Get agent registration token</a><br/>");
					Content.Append("<a href=\"/api/v1/admin/softwaretoken\">Get agent software upload token</a><br/>");
					Content.Append("<a href=\"/api/v1/admin/softwaredownloadtoken\">Get agent software download token</a><br/>");
					Content.Append("<a href=\"/api/v1/admin/configtoken\">Get configuration token</a><br/>");
					Content.Append("<a href=\"/api/v1/admin/chainedjobtoken\">Get chained job token</a><br/>");
					Content.Append("</p>");
				}
				Content.Append(CultureInfo.InvariantCulture, $"<p>Claims for {User.Identity?.Name}:");
				Content.Append("<table>");
				foreach (System.Security.Claims.Claim Claim in User.Claims)
				{
					Content.Append(CultureInfo.InvariantCulture, $"<tr><td>{Claim.Type}</td><td>{Claim.Value}</td></tr>");
				}
				Content.Append("</table>");
				Content.Append("</p>");

				Content.Append(CultureInfo.InvariantCulture, $"<p>Built from Perforce</p>");
			}
			else
			{
				Content.Append("<p><a href=\"/account/login\"><b>Login with OAuth2</b></a></p>");
			}
			Content.Append("</html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/// <summary>
		/// Login to the server
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/account/login")]
		public IActionResult Login()
		{
			return new ChallengeResult(DefaultAuthenticationScheme, new AuthenticationProperties { RedirectUri = "/account" });
		}

		/// <summary>
		/// Logout of the current account
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/account/logout")]
		public async Task<IActionResult> Logout()
		{
			await HttpContext.SignOutAsync(CookieAuthenticationDefaults.AuthenticationScheme);
			try
			{
				await HttpContext.SignOutAsync(DefaultAuthenticationScheme);
			}
			catch
			{
			}

			string Content = $"<html><style>{StyleSheet}</style><body onload=\"setTimeout(function(){{ window.location = '/account'; }}, 2000)\"><p>User has been logged out. Returning to login page.</p></body></html>";
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content };
		}
	}
}
