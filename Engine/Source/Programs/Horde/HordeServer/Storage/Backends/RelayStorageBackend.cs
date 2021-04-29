using EpicGames.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading.Tasks;

namespace HordeServer.Storage.Impl
{
	/// <summary>
	/// Implementation of ILogFileStorage which forwards requests to another server
	/// </summary>
	public sealed class RelayStorageBackend : IStorageBackend
	{
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
		public RelayStorageBackend(IOptionsMonitor<ServerSettings> Settings)
		{
			ServerSettings CurrentSettings = Settings.CurrentValue;
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

			LocalStorage = new FileSystemStorageBackend(Settings);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
			LocalStorage.Dispose();
		}

		/// <inheritdoc/>
		public Task<bool> TouchAsync(string Path)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> ReadAsync(string Path)
		{
			ReadOnlyMemory<byte>? LocalResult = await LocalStorage.ReadAsync(Path);
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
					await LocalStorage.WriteAsync(Path, ResponseData);
					return ResponseData;
				}
			}

			return null;
		}

		/// <inheritdoc/>
		public Task WriteAsync(string Path, ReadOnlyMemory<byte> Data)
		{
			return LocalStorage.WriteAsync(Path, Data);
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			throw new NotImplementedException();
		}
	}
}
