// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading.Tasks;

namespace HordeServer.Storage.Backends
{
	/// <summary>
	/// Implementation of ILogFileStorage which forwards requests to another server
	/// </summary>
	public sealed class RelayStorageBackend : IStorageBackend, IDisposable
	{
		class FileSystemSettings : IFileSystemStorageOptions
		{
			public string? BaseDir { get; set; }
		}

		/// <summary>
		/// The client to connect with
		/// </summary>
		HttpClient Client;

		/// <summary>
		/// The base server URL
		/// </summary>
		Uri ServerUrl;

		/// <summary>
		/// Local storage provider
		/// </summary>
		FileSystemStorageBackend LocalStorage;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings">Settings for the server instance</param>
		public RelayStorageBackend(IOptions<ServerSettings> Settings)
		{
			ServerSettings CurrentSettings = Settings.Value;
			if (CurrentSettings.LogRelayServer == null)
			{
				throw new InvalidDataException("Missing LogRelayServer in server configuration");
			}
			if (CurrentSettings.LogRelayBearerToken == null)
			{
				throw new InvalidDataException("Missing LogRelayBearerToken in server configuration");
			}

			ServerUrl = new Uri(CurrentSettings.LogRelayServer);

			Client = new HttpClient();
			Client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", CurrentSettings.LogRelayBearerToken);

			LocalStorage = new FileSystemStorageBackend(new FileSystemSettings { BaseDir = "RelayCache" });
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
		}

		/// <inheritdoc/>
		public async Task<Stream?> ReadAsync(string Path)
		{
			Stream? LocalResult = await LocalStorage.ReadAsync(Path);
			if (LocalResult != null)
			{
				return LocalResult;
			}

			Uri Url = new Uri(ServerUrl, $"api/v1/debug/storage?path={Path}");
			using (HttpResponseMessage Response = await Client.GetAsync(Url))
			{
				if (Response.IsSuccessStatusCode)
				{
					byte[] ResponseData = await Response.Content.ReadAsByteArrayAsync();
					await LocalStorage.WriteBytesAsync(Path, ResponseData);
					return new MemoryStream(ResponseData);
				}
			}

			return null;
		}

		/// <inheritdoc/>
		public Task WriteAsync(string Path, Stream Stream)
		{
			return LocalStorage.WriteAsync(Path, Stream);
		}

		/// <inheritdoc/>
		public Task<bool> ExistsAsync(string Path)
		{
			throw new NotSupportedException();
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			throw new NotImplementedException();
		}
	}
}
