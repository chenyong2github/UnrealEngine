// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
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

		IUserCollection UserCollection;

		public AnonymousAuthenticationHandler(IUserCollection UserCollection, IOptionsMonitor<AnonymousAuthenticationOptions> Options,
			ILoggerFactory Logger, UrlEncoder Encoder, ISystemClock Clock)
			: base(Options, Logger, Encoder, Clock)
		{
			this.UserCollection = UserCollection;
		}

		protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
		{
			Claim[] Claims =
			{
				new Claim(ClaimTypes.Name, AuthenticationScheme),
				new Claim(Options.AdminClaimType, Options.AdminClaimValue)
			};

			IUser User = await UserCollection.FindOrAddUserByLoginAsync("anonymous", "Anonymous", "anonymous@epicgames.com");

			ClaimsIdentity Identity = new ClaimsIdentity(Claims, Scheme.Name);
			Identity.AddClaim(new Claim(HordeClaimTypes.UserId, User.Id.ToString()));

			ClaimsPrincipal Principal = new ClaimsPrincipal(Identity);
			AuthenticationTicket Ticket = new AuthenticationTicket(Principal, Scheme.Name);

			return AuthenticateResult.Success(Ticket);
		}
	}
}
