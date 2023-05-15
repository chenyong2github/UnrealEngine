// Copyright Epic Games, Inc. All Rights Reserved.

using System.Net.Http.Headers;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using Microsoft.Extensions.Logging;

namespace Horde.Commands
{
	/// <summary>
	/// Base class for commands that require a configured storage client
	/// </summary>
	abstract class StorageCommandBase : Command
	{
		/// <summary>
		/// Base URI to upload to
		/// </summary>
		[CommandLine("-Server=", Description = "Base address of the server to upload to. Should include the base path of the endpoint to use (eg. https://foo/api/v1/storage/default-namespace). Not required if an output file is specified.")]
		public string? Server { get; set; }

		/// <summary>
		/// Auth token to use for the client
		/// </summary>
		[CommandLine("-Token=", Description = "Bearer token for communication with the storage server")]
		public string? Token { get; set; }

		/// <summary>
		/// Creates a new client instance
		/// </summary>
		/// <param name="logger">Logger for output messages</param>
		public IStorageClient CreateStorageClient(ILogger logger)
		{
			if (Server == null)
			{
				throw new CommandLineArgumentException("Missing -Server=... argument for using a comm");
			}
			if (!Server.EndsWith("/", StringComparison.Ordinal))
			{
				Server += "/";
			}

			return new HttpStorageClient(CreateDefaultHttpClient, () => new HttpClient(), logger);
		}

		/// <summary>
		/// Test whether it's possible to create a storage client
		/// </summary>
		public bool CanCreateStorageClient() => Server != null;

		HttpClient CreateDefaultHttpClient()
		{
			HttpClient client = new HttpClient();
			client.BaseAddress = new Uri(Server!);
			if (!String.IsNullOrEmpty(Token))
			{
				client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", Token);
			}
			return client;
		}
	}
}
