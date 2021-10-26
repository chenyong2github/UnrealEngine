// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.OAuth.Claims;
using Microsoft.AspNetCore.Authentication.OpenIdConnect;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Protocols.OpenIdConnect;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Authentication
{
	class OktaDefaults
	{
		public const string AuthenticationScheme = "Okta" + OpenIdConnectDefaults.AuthenticationScheme;
	}

	class OktaHandler : OpenIdConnectHandler
	{
		IUserCollection UserCollection;

		public OktaHandler(IOptionsMonitor<OpenIdConnectOptions> options, ILoggerFactory logger, HtmlEncoder htmlEncoder, UrlEncoder encoder, ISystemClock clock, IUserCollection UserCollection)
			: base(options, logger, htmlEncoder, encoder, clock)
		{
			this.UserCollection = UserCollection;
		}

		public static void AddUserInfoClaims(JsonElement UserInfo, ClaimsIdentity Identity)
		{
			JsonElement UserElement;
			if (UserInfo.TryGetProperty("preferred_username", out UserElement))
			{
				if (Identity.FindFirst(ClaimTypes.Name) == null)
				{
					Identity.AddClaim(new Claim(ClaimTypes.Name, UserElement.ToString()!));
				}
				if (Identity.FindFirst(HordeClaimTypes.User) == null)
				{
					Identity.AddClaim(new Claim(HordeClaimTypes.User, UserElement.ToString()!));
				}
				if (Identity.FindFirst(HordeClaimTypes.PerforceUser) == null)
				{
					Identity.AddClaim(new Claim(HordeClaimTypes.PerforceUser, UserElement.ToString()!));
				}
			}

			JsonElement GroupsElement;
			if (UserInfo.TryGetProperty("groups", out GroupsElement) && GroupsElement.ValueKind == JsonValueKind.Array)
			{
				for (int Idx = 0; Idx < GroupsElement.GetArrayLength(); Idx++)
				{
					Identity.AddClaim(new Claim(ClaimTypes.Role, GroupsElement[Idx].ToString()!));
				}
			}

			JsonElement EmailElement;
			if (UserInfo.TryGetProperty("email", out EmailElement) && Identity.FindFirst(ClaimTypes.Email) == null)
			{
				Identity.AddClaim(new Claim(ClaimTypes.Email, EmailElement.ToString()!));
			}
		}

		protected override async Task<HandleRequestResult> HandleRemoteAuthenticateAsync()
		{
			// Authenticate with the OIDC provider
			HandleRequestResult Result = await base.HandleRemoteAuthenticateAsync();
			if (!Result.Succeeded)
			{
				return Result;
			}

			ClaimsIdentity? Identity = (ClaimsIdentity?)Result.Principal?.Identity;
			if (Identity == null)
			{
				return HandleRequestResult.Fail("No identity specified");
			}

			string Login = Identity.FindFirst(ClaimTypes.Name)!.Value;
			string? Name = Identity.FindFirst("name")?.Value;
			string? Email = Identity.FindFirst(ClaimTypes.Email)?.Value;

			IUser User = await UserCollection.FindOrAddUserByLoginAsync(Login, Name, Email);
			Identity.AddClaim(new Claim(HordeClaimTypes.UserId, User.Id.ToString()));

			await UserCollection.UpdateClaimsAsync(User.Id, Identity.Claims.Select(x => new UserClaim(x.Type, x.Value)));

			return Result;
		}
	}

	static class OktaExtensions
	{
		class MapRolesClaimAction : ClaimAction
		{
			public MapRolesClaimAction()
				: base(ClaimTypes.Role, ClaimTypes.Role)
			{
			}

			public override void Run(JsonElement UserData, ClaimsIdentity Identity, string Issuer)
			{
				OktaHandler.AddUserInfoClaims(UserData, Identity);
			}
		}

		static void ApplyDefaultOktaOptions(OpenIdConnectOptions Options, Action<OpenIdConnectOptions> Handler)
		{
			Options.Scope.Add("profile");
			Options.Scope.Add("groups");
			Options.Scope.Add("email");
			Options.ResponseType = OpenIdConnectResponseType.Code;
			Options.GetClaimsFromUserInfoEndpoint = true;
			Options.SaveTokens = true;
			Options.TokenValidationParameters.NameClaimType = "name";
			Options.ClaimActions.Add(new MapRolesClaimAction());

			Handler(Options);
		}

		public static void AddOkta(this AuthenticationBuilder Builder, string AuthenticationScheme, string DisplayName, Action<OpenIdConnectOptions> Handler)
		{
			Builder.Services.TryAddEnumerable(ServiceDescriptor.Singleton<IPostConfigureOptions<OpenIdConnectOptions>, OpenIdConnectPostConfigureOptions>());
			Builder.AddRemoteScheme<OpenIdConnectOptions, OktaHandler>(AuthenticationScheme, DisplayName, Options => ApplyDefaultOktaOptions(Options, Handler));
		}
	}
}
