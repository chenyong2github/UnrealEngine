// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http;
using System.Net.Http.Headers;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Services
{
	/// <summary>
	/// Interface for creating <see cref="IStorageClient"/> instances
	/// </summary>
	interface IServerStorageFactory
	{
		/// <summary>
		/// Creates a storage client which authenticates using tokens for the current session
		/// </summary>
		/// <param name="session">The current session</param>
		/// <param name="baseUrl">Base URL for writing blobs on the server</param>
		/// <returns>New logger instance</returns>
		IStorageClient CreateStorageClient(ISession session, string baseUrl);
	}

	/// <summary>
	/// Implementation of <see cref="IServerStorageFactory"/> which constructs objects that communicate via HTTP
	/// </summary>
	class HttpServerStorageFactory : IServerStorageFactory
	{
		readonly IHttpClientFactory _httpClientFactory;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public HttpServerStorageFactory(IHttpClientFactory httpClientFactory, ILogger<HttpStorageClient> logger)
		{
			_httpClientFactory = httpClientFactory;
			_logger = logger;
		}

		/// <inheritdoc/>
		public IStorageClient CreateStorageClient(ISession session, string baseUrl)
		{
			return new HttpStorageClient(() => CreateHttpClient(session, baseUrl), CreateHttpRedirectClient, _logger);
		}

		HttpClient CreateHttpClient(ISession session, string baseUrl)
		{
			if (!baseUrl.EndsWith("/", StringComparison.Ordinal))
			{
				baseUrl += "/";
			}

			HttpClient httpClient = _httpClientFactory.CreateClient(HttpStorageClient.HttpClientName);
			httpClient.BaseAddress = new Uri(session.ServerUrl, baseUrl);
			httpClient.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);
			return httpClient;
		}

		HttpClient CreateHttpRedirectClient()
		{
			return _httpClientFactory.CreateClient(HttpStorageClient.HttpClientName);
		}
	}
}
