// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Auth;
using EpicGames.Horde.Storage.Impl;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Auth type for the storage system
	/// </summary>
	public enum StorageAuthType
	{
		/// <summary>
		/// No auth necessary
		/// </summary>
		None,

		/// <summary>
		/// Use OAuth2 for auth
		/// </summary>
		OAuth2,
	}

	/// <summary>
	/// Settings to configure a connected Horde.Storage instance
	/// </summary>
	[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1056:URI-like properties should not be strings")]
	public class StorageSettings
	{
		/// <summary>
		/// URL for the storage service
		/// </summary>
		public string Url { get; set; } = String.Empty;

		/// <summary>
		/// Options for OAuth2
		/// </summary>
		public OAuth2Options? OAuth2 { get; set; }
	}

	/// <summary>
	/// Extension methods for configuring Horde.Storage
	/// </summary>
	public static class StorageExtensions
	{
		/// <summary>
		/// Registers services for Horde Storage
		/// </summary>
		/// <param name="Services">The current service collection</param>
		public static void AddHordeStorage(this IServiceCollection Services)
		{
			Services.AddOptions<StorageSettings>();

			Services.AddSingleton<NullAuthProvider<HttpStorageClient>>();

			Services.AddSingleton<OAuth2AuthProviderFactory<HttpStorageClient>>();
			Services.AddHttpClient<OAuth2AuthProviderFactory<HttpStorageClient>>();

			Services.AddSingleton<IAuthProvider<HttpStorageClient>>(ServiceProvider => CreateAuthProvider(ServiceProvider));

			Services.AddTransient<IStorageClient, HttpStorageClient>();
			Services.AddHttpClient<IStorageClient, HttpStorageClient>();
		}

		/// <summary>
		/// Registers services for Horde Storage
		/// </summary>
		/// <param name="Services">The current service collection</param>
		/// <param name="Configure">Callback for configuring the storage service</param>
		public static void AddHordeStorage(this IServiceCollection Services, Action<StorageSettings> Configure)
		{
			AddHordeStorage(Services);
			Services.Configure<StorageSettings>(Configure);
		}

		/// <summary>
		/// Creates an auth provider based on the configured settings
		/// </summary>
		/// <param name="Services"></param>
		/// <returns></returns>
		static IAuthProvider<HttpStorageClient> CreateAuthProvider(IServiceProvider Services)
		{
			StorageSettings Settings = Services.GetRequiredService<IOptions<StorageSettings>>().Value;
			if (Settings.OAuth2 != null)
			{
				return Services.GetRequiredService<OAuth2AuthProviderFactory<HttpStorageClient>>().Create(Settings.OAuth2);
			}
			else
			{
				return Services.GetRequiredService<NullAuthProvider<HttpStorageClient>>();
			}
		}
	}
}
