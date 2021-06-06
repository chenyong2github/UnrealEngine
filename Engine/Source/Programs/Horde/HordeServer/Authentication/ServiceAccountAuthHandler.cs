// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Authentication;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Text.Encodings.Web;
using System.Threading.Tasks;
using HordeServer.Collections;
using HordeServer.Models;
using Microsoft.Net.Http.Headers;

namespace HordeServer.Authentication
{
	class ServiceAccountAuthOptions : AuthenticationSchemeOptions
	{
	}

	class ServiceAccountAuthHandler : AuthenticationHandler<ServiceAccountAuthOptions>
	{
		public const string AuthenticationScheme = "ServiceAccount";
		public const string Prefix = "ServiceAccount";

		private readonly IServiceAccountCollection ServiceAccounts;

		public ServiceAccountAuthHandler(IOptionsMonitor<ServiceAccountAuthOptions> Options,
			ILoggerFactory Logger, UrlEncoder Encoder, ISystemClock Clock, IServiceAccountCollection ServiceAccounts)
			: base(Options, Logger, Encoder, Clock)
		{
			this.ServiceAccounts = ServiceAccounts;
		}
		
		protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
		{
			if (!Context.Request.Headers.TryGetValue(HeaderNames.Authorization, out var HeaderValue))
			{
				return AuthenticateResult.NoResult();
			}

			if (HeaderValue.Count < 1)
			{
				return AuthenticateResult.NoResult();
			}

			if (!HeaderValue[0].StartsWith(Prefix, StringComparison.Ordinal))
			{
				return AuthenticateResult.NoResult();
			}
			
			string Token = HeaderValue[0].Replace(Prefix, "", StringComparison.Ordinal).Trim();
			IServiceAccount? ServiceAccount = await ServiceAccounts.GetBySecretTokenAsync(Token);

			if (ServiceAccount == null)
			{
				return AuthenticateResult.Fail($"Service account for token {Token} not found");
			}

			List<Claim> Claims = new List<Claim>(10);
			Claims.Add(new Claim(ClaimTypes.Name, AuthenticationScheme));
			Claims.AddRange(ServiceAccount.GetClaims().Select(ClaimPair => new Claim(ClaimPair.Type, ClaimPair.Value)));

			ClaimsIdentity Identity = new ClaimsIdentity(Claims, Scheme.Name);
			ClaimsPrincipal Principal = new ClaimsPrincipal(Identity);
			AuthenticationTicket Ticket = new AuthenticationTicket(Principal, Scheme.Name);

			return AuthenticateResult.Success(Ticket);
		}
	}

	static class ServiceAccountExtensions
	{
		public static AuthenticationBuilder AddServiceAccount(this AuthenticationBuilder Builder, Action<ServiceAccountAuthOptions> Config)
		{
			return Builder.AddScheme<ServiceAccountAuthOptions, ServiceAccountAuthHandler>(ServiceAccountAuthHandler.AuthenticationScheme, Config);
		}
	}
}
