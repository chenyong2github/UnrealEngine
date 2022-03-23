// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IdentityModel.Tokens.Jwt;
using System.Text.Encodings.Web;
using System.Threading.Tasks;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Build.Authentication
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
		readonly DatabaseService _databaseService;

		public HordeJwtBearerHandler(ILoggerFactory logger, UrlEncoder encoder, ISystemClock clock, DatabaseService databaseService, IOptionsMonitorCache<JwtBearerOptions> optionsCache)
			: base(GetOptionsMonitor(databaseService, optionsCache), logger, encoder, clock)
		{
			_databaseService = databaseService;
		}

		private static IOptionsMonitor<JwtBearerOptions> GetOptionsMonitor(DatabaseService databaseService, IOptionsMonitorCache<JwtBearerOptions> optionsCache)
		{
			ConfigureNamedOptions<JwtBearerOptions> namedOptions = new ConfigureNamedOptions<JwtBearerOptions>(AuthenticationScheme, options => Configure(options, databaseService));
			OptionsFactory<JwtBearerOptions> optionsFactory = new OptionsFactory<JwtBearerOptions>(new[] { namedOptions }, Array.Empty<IPostConfigureOptions<JwtBearerOptions>>());
			return new OptionsMonitor<JwtBearerOptions>(optionsFactory, Array.Empty<IOptionsChangeTokenSource<JwtBearerOptions>>(), optionsCache);
		}

		private static void Configure(JwtBearerOptions options, DatabaseService databaseService)
		{
			options.TokenValidationParameters.ValidateAudience = false;

			options.TokenValidationParameters.RequireExpirationTime = false;
			options.TokenValidationParameters.ValidateLifetime = true;

			options.TokenValidationParameters.ValidIssuer = databaseService.JwtIssuer;
			options.TokenValidationParameters.ValidateIssuer = true;

			options.TokenValidationParameters.ValidateIssuerSigningKey = true;
			options.TokenValidationParameters.IssuerSigningKey = databaseService.JwtSigningKey;
		}

		protected override Task<AuthenticateResult> HandleAuthenticateAsync()
		{
			// Silent fail if this JWT is not issued by the server
			string? token;
			if (!JwtUtils.TryGetBearerToken(Request, "Bearer ", out token))
			{
				return Task.FromResult(AuthenticateResult.NoResult());
			}

			// Validate that it's from the correct issuer
			JwtSecurityToken? jwtToken;
			if (!JwtUtils.TryParseJwt(token, out jwtToken) || !String.Equals(jwtToken.Issuer, _databaseService.JwtIssuer, StringComparison.Ordinal))
			{
				return Task.FromResult(AuthenticateResult.NoResult());
			}

			// Pass it to the base class
			return base.HandleAuthenticateAsync();
		}
	}
}
