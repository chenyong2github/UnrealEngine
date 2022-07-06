// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Options;

namespace Horde.Build.Storage.Backends
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
		readonly HttpClient _client;

		/// <summary>
		/// The base server URL
		/// </summary>
		readonly Uri _serverUrl;

		/// <summary>
		/// Local storage provider
		/// </summary>
		readonly FileSystemStorageBackend _localStorage;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="settings">Settings for the server instance</param>
		public RelayStorageBackend(IOptions<ServerSettings> settings)
		{
			ServerSettings currentSettings = settings.Value;
			if (currentSettings.LogRelayServer == null)
			{
				throw new InvalidDataException("Missing LogRelayServer in server configuration");
			}
			if (currentSettings.LogRelayBearerToken == null)
			{
				throw new InvalidDataException("Missing LogRelayBearerToken in server configuration");
			}

			_serverUrl = new Uri(currentSettings.LogRelayServer);

			_client = new HttpClient();
			_client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", currentSettings.LogRelayBearerToken);

			_localStorage = new FileSystemStorageBackend(new FileSystemSettings { BaseDir = "RelayCache" });
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_client.Dispose();
		}

		/// <inheritdoc/>
		public async Task<Stream?> ReadAsync(string path, CancellationToken cancellationToken)
		{
			Stream? localResult = await _localStorage.ReadAsync(path, cancellationToken);
			if (localResult != null)
			{
				return localResult;
			}

			Uri url = new Uri(_serverUrl, $"api/v1/debug/storage?path={path}");
			using (HttpResponseMessage response = await _client.GetAsync(url, cancellationToken))
			{
				if (response.IsSuccessStatusCode)
				{
					byte[] responseData = await response.Content.ReadAsByteArrayAsync(cancellationToken);
					await _localStorage.WriteBytesAsync(path, responseData, cancellationToken);
					return new MemoryStream(responseData);
				}
			}

			return null;
		}

		/// <inheritdoc/>
		public Task WriteAsync(string path, Stream stream, CancellationToken cancellationToken)
		{
			return _localStorage.WriteAsync(path, stream, cancellationToken);
		}

		/// <inheritdoc/>
		public Task<bool> ExistsAsync(string path, CancellationToken cancellationToken)
		{
			throw new NotSupportedException();
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string path, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}
	}
}
