// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Http;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Helper functions for dealing with JWTs
	/// </summary>
	public static class JwtUtils
	{
		/// <summary>
		/// Gets the bearer token from an HTTP request
		/// </summary>
		/// <param name="Request">The request to read from</param>
		/// <param name="BearerPrefix">The bearer token prefix, ex. "Bearer "</param>
		/// <param name="Token">On success, receives the bearer token</param>
		/// <returns>True if the bearer token was read</returns>
		public static bool TryGetBearerToken(HttpRequest Request, string BearerPrefix, [NotNullWhen(true)] out string? Token)
		{
			// Get the authorization header
			string? Authorization = Request.Headers["Authorization"];
			if (String.IsNullOrEmpty(Authorization))
			{
				Token = null;
				return false;
			}

			// Check if it's a bearer token				
			if (!Authorization.StartsWith(BearerPrefix, StringComparison.OrdinalIgnoreCase))
			{
				Token = null;
				return false;
			}

			// Get the token
			Token = Authorization.Substring(BearerPrefix.Length).Trim();
			return true;
		}

		/// <summary>
		/// Tries to parse a JWT and check the issuer matches
		/// </summary>
		/// <param name="Token">The token to parse</param>
		/// <param name="JwtToken">On success, receives the parsed JWT</param>
		/// <returns>True if the jwt was parsed</returns>
		public static bool TryParseJwt(string Token, [NotNullWhen(true)] out JwtSecurityToken? JwtToken)
		{
			// Check if it's a JWT
			JwtSecurityTokenHandler Handler = new JwtSecurityTokenHandler();
			if (!Handler.CanReadToken(Token))
			{
				JwtToken = null;
				return false;
			}

			// Try to parse the JWT
			try
			{
				JwtToken = Handler.ReadJwtToken(Token);
				return true;
			}
			catch (JsonException)
			{
				JwtToken = null;
				return false;
			}
		}
	}
}
