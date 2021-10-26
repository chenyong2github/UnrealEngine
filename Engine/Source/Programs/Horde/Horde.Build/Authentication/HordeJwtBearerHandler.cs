// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Text.Encodings.Web;
using System.Threading.Tasks;

namespace HordeServer.Authentication
{
	/// <summary>
	/// JWT handler for server-issued bearer tokens. These tokens are signed using a randomly generated key per DB instance.
	/// </summary>
	class HordeJwtBearerHandler : JwtBearerHandler
	{
		/// <summary>
		/// Default name of the authentication scheme
		/// </summary>
		public const string AuthenticationScheme = "ServerJwt";

		DatabaseService DatabaseService;

		public HordeJwtBearerHandler(ILoggerFactory Logger, UrlEncoder Encoder, ISystemClock Clock, DatabaseService DatabaseService, IOptionsMonitorCache<JwtBearerOptions> OptionsCache)
			: base(GetOptionsMonitor(DatabaseService, OptionsCache), Logger, Encoder, Clock)
		{
			this.DatabaseService = DatabaseService;
		}

		private static IOptionsMonitor<JwtBearerOptions> GetOptionsMonitor(DatabaseService DatabaseService, IOptionsMonitorCache<JwtBearerOptions> OptionsCache)
		{
			ConfigureNamedOptions<JwtBearerOptions> NamedOptions = new ConfigureNamedOptions<JwtBearerOptions>(AuthenticationScheme, Options => Configure(Options, DatabaseService));
			OptionsFactory<JwtBearerOptions> OptionsFactory = new OptionsFactory<JwtBearerOptions>(new[] { NamedOptions }, Array.Empty<IPostConfigureOptions<JwtBearerOptions>>());
			return new OptionsMonitor<JwtBearerOptions>(OptionsFactory, Array.Empty<IOptionsChangeTokenSource<JwtBearerOptions>>(), OptionsCache);
		}

		private static void Configure(JwtBearerOptions Options, DatabaseService DatabaseService)
		{
			Options.TokenValidationParameters.ValidateAudience = false;

			Options.TokenValidationParameters.RequireExpirationTime = false;
			Options.TokenValidationParameters.ValidateLifetime = true;

			Options.TokenValidationParameters.ValidIssuer = DatabaseService.JwtIssuer;
			Options.TokenValidationParameters.ValidateIssuer = true;

			Options.TokenValidationParameters.ValidateIssuerSigningKey = true;
			Options.TokenValidationParameters.IssuerSigningKey = DatabaseService.JwtSigningKey;
		}

		protected override Task<AuthenticateResult> HandleAuthenticateAsync()
		{
			// Silent fail if this JWT is not issued by the server
			string? Token;
			if (!JwtUtils.TryGetBearerToken(Request, "Bearer ", out Token))
			{
				return Task.FromResult(AuthenticateResult.NoResult());
			}

			// Validate that it's from the correct issuer
			JwtSecurityToken? JwtToken;
			if (!JwtUtils.TryParseJwt(Token, out JwtToken) || !String.Equals(JwtToken.Issuer, DatabaseService.JwtIssuer, StringComparison.Ordinal))
			{
				return Task.FromResult(AuthenticateResult.NoResult());
			}

			// Pass it to the base class
			return base.HandleAuthenticateAsync();
		}
	}
}
