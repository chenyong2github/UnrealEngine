// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http.Headers;
using System.Net.Http;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;
using EpicGames.Horde.Storage.Backends;
using Microsoft.Extensions.Options;
using EpicGames.Core;

namespace Horde.Agent.Utility
{
	/// <summary>
	/// Class which creates <see cref="IStorageClient"/> instances
	/// </summary>
	class StorageClientFactory : IStorageClientFactory
	{
		readonly IOptions<AgentSettings> _settings;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public StorageClientFactory(IOptions<AgentSettings> settings, ILogger<IStorageClient> logger)
		{
			_settings = settings;
			_logger = logger;
		}

		/// <summary>
		/// Creates a client for the given namespace
		/// </summary>
		/// <param name="namespaceId">Namespace to get a client for</param>
		/// <returns>New storage client instance</returns>
		public IStorageClient Create(NamespaceId namespaceId)
		{
			if (_settings.Value.UseLocalStorageClient)
			{
				return new FileStorageClient(DirectoryReference.Combine(Program.DataDir, "Storage", namespaceId.ToString()), _logger);
			}
			else
			{
				return new HttpStorageClient(() => CreateDefaultHttpClient(namespaceId), () => new HttpClient(), _logger);
			}
		}

		HttpClient CreateDefaultHttpClient(NamespaceId namespaceId)
		{
			ServerProfile profile = _settings.Value.GetCurrentServerProfile();

			HttpClient client = new HttpClient();
			client.BaseAddress = new Uri(profile.Url, $"api/v1/storage/{namespaceId}/");
			if (!String.IsNullOrEmpty(profile.Token))
			{
				client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", profile.Token);
			}

			return client;
		}

	}
}
