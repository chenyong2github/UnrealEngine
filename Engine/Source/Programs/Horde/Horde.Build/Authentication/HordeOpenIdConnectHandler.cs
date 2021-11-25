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
using System.Linq;
using System.Security.Claims;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Authentication
{
	class HordeOpenIdConnectHandler : OpenIdConnectHandler
	{
		IUserCollection UserCollection;

		public HordeOpenIdConnectHandler(IOptionsMonitor<OpenIdConnectOptions> Options, ILoggerFactory Logger, HtmlEncoder HtmlEncoder, UrlEncoder Encoder, ISystemClock Clock, IUserCollection UserCollection)
			: base(Options, Logger, HtmlEncoder, Encoder, Clock)
		{
			this.UserCollection = UserCollection;
		}

		/// <summary>
		/// Try to map a field found in user info as a claim
		///
		/// If the claim is already set, it is skipped.
		/// </summary>
		/// <param name="UserInfo">User info from the OIDC /userinfo endpoint </param>
		/// <param name="Identity">Identity which claims to modify</param>
		/// <param name="ClaimName">Name of claim</param>
		/// <param name="UserInfoFields">List of field names to try when mapping</param>
		/// <exception cref="Exception">Thrown if no field name matches</exception>
		private static void MapUserInfoFieldToClaim(JsonElement UserInfo, ClaimsIdentity Identity, string ClaimName, string[] UserInfoFields)
		{
			if (Identity.FindFirst(ClaimName) != null)
			{
				return;
			}

			foreach (string UserDataField in UserInfoFields)
			{
				if (UserInfo.TryGetProperty(UserDataField, out JsonElement FieldElement))
				{
					Identity.AddClaim(new Claim(ClaimName, FieldElement.ToString()));
					return;
				}
			}

			string Message = $"Unable to map a field from user info to claim '{ClaimName}' using list [{string.Join(", ", UserInfoFields)}].";
			Message += " UserInfo: " + UserInfo;
			throw new Exception(Message);
		}

		public static void AddUserInfoClaims(ServerSettings Settings, JsonElement UserInfo, ClaimsIdentity Identity)
		{
			MapUserInfoFieldToClaim(UserInfo, Identity, ClaimTypes.Name, Settings.OidcClaimNameMapping);
			MapUserInfoFieldToClaim(UserInfo, Identity, ClaimTypes.Email, Settings.OidcClaimEmailMapping);
			MapUserInfoFieldToClaim(UserInfo, Identity, HordeClaimTypes.User, Settings.OidcClaimHordeUserMapping);
			MapUserInfoFieldToClaim(UserInfo, Identity, HordeClaimTypes.PerforceUser, Settings.OidcClaimHordePerforceUserMapping);

			if (UserInfo.TryGetProperty("groups", out JsonElement GroupsElement) && GroupsElement.ValueKind == JsonValueKind.Array)
			{
				for (int Idx = 0; Idx < GroupsElement.GetArrayLength(); Idx++)
				{
					Identity.AddClaim(new Claim(ClaimTypes.Role, GroupsElement[Idx].ToString()!));
				}
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

	static class OpenIdConnectHandlerExtensions
	{
		class MapRolesClaimAction : ClaimAction
		{
			private ServerSettings Settings;
			public MapRolesClaimAction(ServerSettings Settings) : base(ClaimTypes.Role, ClaimTypes.Role) { this.Settings = Settings; }
			public override void Run(JsonElement UserData, ClaimsIdentity Identity, string Issuer) { HordeOpenIdConnectHandler.AddUserInfoClaims(Settings, UserData, Identity); }
		}

		static void ApplyDefaultOptions(ServerSettings Settings, OpenIdConnectOptions Options, Action<OpenIdConnectOptions> Handler)
		{
			Options.ResponseType = OpenIdConnectResponseType.Code;
			Options.GetClaimsFromUserInfoEndpoint = true;
			Options.SaveTokens = true;
			Options.TokenValidationParameters.NameClaimType = "name";
			Options.ClaimActions.Add(new MapRolesClaimAction(Settings));

			Handler(Options);
		}

		public static void AddHordeOpenId(this AuthenticationBuilder Builder, ServerSettings Settings, string AuthenticationScheme, string DisplayName, Action<OpenIdConnectOptions> Handler)
		{
			Builder.Services.TryAddEnumerable(ServiceDescriptor.Singleton<IPostConfigureOptions<OpenIdConnectOptions>, OpenIdConnectPostConfigureOptions>());
			Builder.AddRemoteScheme<OpenIdConnectOptions, HordeOpenIdConnectHandler>(AuthenticationScheme, DisplayName, Options => ApplyDefaultOptions(Settings, Options, Handler));
		}
	}
}
