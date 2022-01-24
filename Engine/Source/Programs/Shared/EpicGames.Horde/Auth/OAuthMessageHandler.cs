// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
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
	/// Http message handler which adds an OAuth authorization header using a cached/periodically refreshed bearer token
	/// </summary>
	public class OAuthMessageHandler : DelegatingHandler
	{
		class ClientCredentialsResponse
		{
			public string? access_token { get; set; }
			public string? token_type { get; set; }
			public int? expires_in { get; set; }
			public string? scope { get; set; }
		}

		readonly string AuthUrl;
		readonly string GrantType;
		readonly string ClientId;
		readonly string ClientSecret;
		readonly string Scope;

		HttpClient Client;
		string CachedAccessToken = String.Empty;
		DateTime ExpiresAt = DateTime.MinValue;

		public OAuthMessageHandler(HttpClient Client, string AuthUrl, string GrantType, string ClientId, string ClientSecret, string Scope)
		{
			this.Client = Client;
			this.AuthUrl = AuthUrl;
			this.GrantType = GrantType;
			this.ClientId = ClientId;
			this.ClientSecret = ClientSecret;
			this.Scope = Scope;
		}

		/// <inheritdoc/>
		protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
		{
			if (DateTime.UtcNow > ExpiresAt)
			{
				await UpdateAccessTokenAsync();
			}

			request.Headers.Add("Authorization", $"Bearer {CachedAccessToken}");
			return await base.SendAsync(request, cancellationToken);
		}

		/// <summary>
		/// Updates the current access token
		/// </summary>
		/// <returns></returns>
		async Task UpdateAccessTokenAsync()
		{
			KeyValuePair<string, string>[] Content = new KeyValuePair<string, string>[]
			{
				new KeyValuePair<string, string>("grant_type", GrantType),
				new KeyValuePair<string, string>("client_id", ClientId),
				new KeyValuePair<string, string>("client_secret", ClientSecret),
				new KeyValuePair<string, string>("scope", Scope)
			};

			try
			{
				using HttpRequestMessage Message = new HttpRequestMessage(HttpMethod.Post, AuthUrl);
				Message.Content = new FormUrlEncodedContent(Content);

				HttpResponseMessage Response = await Client.SendAsync(Message);
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
}
