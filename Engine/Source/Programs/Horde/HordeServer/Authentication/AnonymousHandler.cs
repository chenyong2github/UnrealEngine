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

namespace HordeServer.Authentication
{
	class AnonymousAuthenticationOptions : AuthenticationSchemeOptions
	{
		public string? AdminClaimType { get; set; }
		public string? AdminClaimValue { get; set; }
	}

	class AnonymousAuthenticationHandler : AuthenticationHandler<AnonymousAuthenticationOptions>
	{
		public const string AuthenticationScheme = "Anonymous";

		public AnonymousAuthenticationHandler(IOptionsMonitor<AnonymousAuthenticationOptions> Options,
			ILoggerFactory Logger, UrlEncoder Encoder, ISystemClock Clock)
			: base(Options, Logger, Encoder, Clock)
		{
		}

		protected override Task<AuthenticateResult> HandleAuthenticateAsync()
		{
			Claim[] Claims =
			{
					new Claim(ClaimTypes.Name, AuthenticationScheme),
					new Claim(Options.AdminClaimType, Options.AdminClaimValue)
				};
			ClaimsIdentity Identity = new ClaimsIdentity(Claims, Scheme.Name);

			ClaimsPrincipal Principal = new ClaimsPrincipal(Identity);
			AuthenticationTicket Ticket = new AuthenticationTicket(Principal, Scheme.Name);

			return Task.FromResult(AuthenticateResult.Success(Ticket));
		}
	}
}
