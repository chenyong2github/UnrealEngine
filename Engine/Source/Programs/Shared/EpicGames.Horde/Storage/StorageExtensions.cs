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
	/// Settings to configure a connected Horde.Storage instance
	/// </summary>
	[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1056:URI-like properties should not be strings")]
	public class StorageOptions : HttpServiceClientOptions
	{
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
			Services.AddOptions<StorageOptions>();

			Services.AddScoped<IStorageClient, HttpStorageClient>();
			Services.AddHttpClientWithAuth<IStorageClient, HttpStorageClient>(ServiceProvider => ServiceProvider.GetRequiredService<IOptions<StorageOptions>>().Value);
		}

		/// <summary>
		/// Registers services for Horde Storage
		/// </summary>
		/// <param name="Services">The current service collection</param>
		/// <param name="Configure">Callback for configuring the storage service</param>
		public static void AddHordeStorage(this IServiceCollection Services, Action<StorageOptions> Configure)
		{
			AddHordeStorage(Services);
			Services.Configure<StorageOptions>(Configure);
		}
	}
}
