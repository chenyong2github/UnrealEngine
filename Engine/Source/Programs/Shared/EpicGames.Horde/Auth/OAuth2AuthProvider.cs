// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Auth
{
	/// <summary>
	/// Exception thrown due to failed authorization
	/// </summary>
	public class AuthenticationException : Exception
	{
		public AuthenticationException(string Message, Exception? InnerException)
			: base(Message, InnerException)
		{
		}
	}

	/// <summary>
	/// Options for authenticating particular requests
	/// </summary>
	public class OAuth2Options
	{
		/// <summary>
		/// Url of the auth server
		/// </summary>
		public string AuthUrl { get; set; } = String.Empty;

		/// <summary>
		/// Type of grant
		/// </summary>
		public string GrantType { get; set; } = String.Empty;

		/// <summary>
		/// Client id
		/// </summary>
		public string ClientId { get; set; } = String.Empty;

		/// <summary>
		/// Client secret
		/// </summary>
		public string ClientSecret { get; set; } = String.Empty;

		/// <summary>
		/// Scope of the token
		/// </summary>
		public string Scope { get; set; } = String.Empty;
	}

	/// <summary>
	/// Http message handler which adds an OAuth authorization header using a cached/periodically refreshed bearer token
	/// </summary>
	public class OAuth2AuthProvider : IAuthProvider
	{
		class ClientCredentialsResponse
		{
			public string? access_token { get; set; }
			public string? token_type { get; set; }
			public int? expires_in { get; set; }
			public string? scope { get; set; }
		}

		HttpClient Client;
		OAuth2Options Options;
		string CachedAccessToken = String.Empty;
		DateTime ExpiresAt = DateTime.MinValue;

		public OAuth2AuthProvider(HttpClient Client, OAuth2Options Options)
		{
			this.Client = Client;
			this.Options = Options;
		}

		/// <inheritdoc/>
		public async Task AddAuthorizationAsync(HttpRequestMessage Request, CancellationToken CancellationToken)
		{
			if (DateTime.UtcNow > ExpiresAt)
			{
				await UpdateAccessTokenAsync(CancellationToken);
			}

			Request.Headers.Add("Authorization", $"Bearer {CachedAccessToken}");
		}

		/// <summary>
		/// Updates the current access token
		/// </summary>
		/// <returns></returns>
		async Task UpdateAccessTokenAsync(CancellationToken CancellationToken)
		{
			KeyValuePair<string, string>[] Content = new KeyValuePair<string, string>[]
			{
				new KeyValuePair<string, string>("grant_type", Options.GrantType),
				new KeyValuePair<string, string>("client_id", Options.ClientId),
				new KeyValuePair<string, string>("client_secret", Options.ClientSecret),
				new KeyValuePair<string, string>("scope", Options.Scope)
			};

			try
			{
				using HttpRequestMessage Message = new HttpRequestMessage(HttpMethod.Post, Options.AuthUrl);
				Message.Content = new FormUrlEncodedContent(Content);

				HttpResponseMessage Response = await Client.SendAsync(Message, CancellationToken);
				if (!Response.IsSuccessStatusCode)
				{
					throw new AuthenticationException($"Authentication failed. Response: {Response.Content}", null);
				}

				byte[] ResponseData = await Response.Content.ReadAsByteArrayAsync();
				ClientCredentialsResponse Result = JsonSerializer.Deserialize<ClientCredentialsResponse>(ResponseData)!;

				string? accessToken = Result?.access_token;
				if (string.IsNullOrEmpty(accessToken))
				{
					throw new AuthenticationException("The authentication token received by the server is null or empty. Body received was: " + Encoding.UTF8.GetString(ResponseData), null);
				}
				CachedAccessToken = accessToken;
				// renew after half the renewal time
				ExpiresAt = DateTime.UtcNow + TimeSpan.FromSeconds((Result?.expires_in ?? 3200) / 2.0);
			}
			catch (WebException Ex)
			{
				throw new AuthenticationException("Unable to authenticate.", Ex);
			}
		}
	}

	/// <summary>
	/// Factory for creating OAuth2AuthProvider instances from a set of options
	/// </summary>
	public class OAuth2AuthProviderFactory<T>
	{
		HttpClient HttpClient;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="HttpClient"></param>
		public OAuth2AuthProviderFactory(HttpClient HttpClient)
		{
			this.HttpClient = HttpClient;
		}

		/// <summary>
		/// Create an instance of the auth provider
		/// </summary>
		/// <param name="Options"></param>
		/// <returns></returns>
		public IAuthProvider<T> Create(OAuth2Options Options) => new TypedAuthProvider<T>(new OAuth2AuthProvider(HttpClient, Options));
	}
}
