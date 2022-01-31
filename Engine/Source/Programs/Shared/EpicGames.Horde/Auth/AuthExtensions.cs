// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.DependencyInjection;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Net.Http;
using System.Text;

namespace EpicGames.Horde.Auth
{
	/// <summary>
	/// Base class for configuring HTTP service clients
	/// </summary>
	public class HttpServiceClientOptions : IOAuthOptions, ITokenAuthOptions
	{
		/// <summary>
		/// Base address for http requests
		/// </summary>
		[Required]
		public string Url { get; set; } = String.Empty;

		#region OAuth2

		/// <inheritdoc/>
		public string AuthUrl { get; set; } = String.Empty;

		/// <inheritdoc/>
		public string GrantType { get; set; } = String.Empty;

		/// <inheritdoc/>
		public string ClientId { get; set; } = String.Empty;

		/// <inheritdoc/>
		public string ClientSecret { get; set; } = String.Empty;

		/// <inheritdoc/>
		public string Scope { get; set; } = String.Empty;

		#endregion

		#region Bearer token

		/// <inheritdoc/>
		public string Token { get; set; } = String.Empty;

		#endregion
	}

	internal static class AuthExtensions
	{
		public static void AddHttpClientWithAuth<TClient, TImplementation>(this IServiceCollection Services, Func<IServiceProvider, HttpServiceClientOptions> GetOptions) 
			where TClient : class 
			where TImplementation : class, TClient
		{
			Services.AddScoped<OAuthHandlerFactory>();
			Services.AddHttpClient<OAuthHandlerFactory>();
			Services.AddScoped<OAuthHandler<TImplementation>>(ServiceProvider => ServiceProvider.GetRequiredService<OAuthHandlerFactory>().Create<TImplementation>(GetOptions(ServiceProvider)));

			Services.AddHttpClient<TClient, TImplementation>((ServiceProvider, Client) =>
				{
					HttpServiceClientOptions Options = GetOptions(ServiceProvider);
					Client.BaseAddress = new Uri(Options.Url);
				})
				.ConfigurePrimaryHttpMessageHandler(ServiceProvider =>
				{
					HttpServiceClientOptions Options = GetOptions(ServiceProvider);
					return CreateMessageHandler<TImplementation>(ServiceProvider, Options);
				});
		}

		static HttpMessageHandler CreateMessageHandler<TImplementation>(IServiceProvider ServiceProvider, HttpServiceClientOptions Options)
		{
			if (!String.IsNullOrEmpty(Options.AuthUrl))
			{
				return ServiceProvider.GetRequiredService<OAuthHandler<TImplementation>>();
			}
			else if (!String.IsNullOrEmpty(Options.Token))
			{
				return new TokenHandler<TImplementation>(Options);
			}
			else
			{
				return new HttpClientHandler();
			}
		}
	}
}
