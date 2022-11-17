// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage.Backends
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which communicates with an upstream Horde instance via HTTP.
	/// </summary>
	public class HttpStorageClient : StorageClientBase
	{
		/// <summary>
		/// Name of clients created from the http client factory
		/// </summary>
		public const string HttpClientName = "Horde.HttpStorageClient";

		class WriteBlobResponse
		{
			public BlobLocator Locator { get; set; }
			public Uri? UploadUrl { get; set; }
			public bool? SupportsRedirects { get; set; }
		}

		class ReadRefResponse
		{
			public BlobLocator Blob { get; set; }
			public int ExportIdx { get; set; }
		}

		readonly Func<HttpClient> _createClient;
		readonly Func<HttpClient> _createRedirectClient;
		readonly ILogger _logger;
		bool _supportsUploadRedirects = true;

		/// <summary>
		/// Constructor
		/// </summary>
		public HttpStorageClient(Func<HttpClient> createClient, Func<HttpClient> createRedirectClient, ILogger logger) 
		{
			_createClient = createClient;
			_createRedirectClient = createRedirectClient;
			_logger = logger;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public HttpStorageClient(IHttpClientFactory httpClientFactory, Uri baseAddress, string? bearerToken, ILogger logger)
			: this(() => CreateAuthenticatedClient(httpClientFactory, baseAddress, bearerToken), () => CreateClient(httpClientFactory), logger)
		{
		}

		/// <summary>
		/// Helper method to create an HTTP client from the given factory
		/// </summary>
		static HttpClient CreateClient(IHttpClientFactory httpClientFactory)
		{
			return httpClientFactory.CreateClient(HttpClientName);
		}

		/// <summary>
		/// Helper method to add the base address and auth header to an HTTP client
		/// </summary>
		static HttpClient CreateAuthenticatedClient(IHttpClientFactory httpClientFactory, Uri baseAddress, string? bearerToken)
		{
			HttpClient httpClient = CreateClient(httpClientFactory);
			httpClient.BaseAddress = baseAddress;
			if (bearerToken != null)
			{
				httpClient.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", bearerToken);
			}
			return httpClient;
		}

		#region Blobs

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			_logger.LogDebug("Reading {Locator}", locator);
			using (HttpClient httpClient = _createClient())
			{
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"blobs/{locator}"))
				{
					HttpResponseMessage response = await httpClient.SendAsync(request, cancellationToken);
					response.EnsureSuccessStatusCode();
					return await response.Content.ReadAsStreamAsync(cancellationToken);
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
		{
			_logger.LogDebug("Reading {Locator} ({Offset}+{Length})", locator, offset, length);
			using (HttpClient httpClient = _createClient())
			{
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"blobs/{locator}?offset={offset}&length={length}"))
				{
					HttpResponseMessage response = await httpClient.SendAsync(request, cancellationToken);
					response.EnsureSuccessStatusCode();
					return await response.Content.ReadAsStreamAsync(cancellationToken);
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using StreamContent streamContent = new StreamContent(stream);

			if (_supportsUploadRedirects)
			{
				using (HttpClient redirectHttpClient = _createRedirectClient())
				{
					WriteBlobResponse redirectResponse = await SendWriteRequestAsync(null, prefix, cancellationToken);
					if (redirectResponse.UploadUrl != null)
					{
						using (HttpResponseMessage uploadResponse = await redirectHttpClient.PutAsync(redirectResponse.UploadUrl, streamContent, cancellationToken))
						{
							if (!uploadResponse.IsSuccessStatusCode)
							{
								string body = await uploadResponse.Content.ReadAsStringAsync(cancellationToken);
								throw new StorageException($"Unable to upload data to redirected URL: {body}", null);
							}
						}
						_logger.LogDebug("Written {Locator} (using redirect)", redirectResponse.Locator);
						return redirectResponse.Locator;
					}
				}
			}

			WriteBlobResponse response = await SendWriteRequestAsync(streamContent, prefix, cancellationToken);
			_supportsUploadRedirects = response.SupportsRedirects ?? false;
			_logger.LogDebug("Written {Locator} (direct)", response.Locator);
			return response.Locator;
		}

		async Task<WriteBlobResponse> SendWriteRequestAsync(StreamContent? streamContent, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using (HttpClient httpClient = _createClient())
			{
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, $"blobs"))
				{
					using StringContent stringContent = new StringContent(prefix.ToString());

					MultipartFormDataContent form = new MultipartFormDataContent();
					if (streamContent != null)
					{
						form.Add(streamContent, "file", "filename");
					}
					form.Add(stringContent, "prefix");

					request.Content = form;
					using (HttpResponseMessage response = await httpClient.SendAsync(request, cancellationToken))
					{
						response.EnsureSuccessStatusCode();
						WriteBlobResponse? data = await response.Content.ReadFromJsonAsync<WriteBlobResponse>(cancellationToken: cancellationToken);
						return data!;
					}
				}
			}
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override async Task DeleteRefAsync(RefName name, CancellationToken cancellationToken)
		{
			_logger.LogDebug("Deleting ref {RefName}", name);
			using (HttpClient httpClient = _createClient())
			{
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Delete, $"refs/{name}"))
				{
					using (HttpResponseMessage response = await httpClient.SendAsync(request, cancellationToken))
					{
						response.EnsureSuccessStatusCode();
					}
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			using (HttpClient httpClient = _createClient())
			{
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"refs/{name}"))
				{
					using (HttpResponseMessage response = await httpClient.SendAsync(request, cancellationToken))
					{
						if (response.StatusCode == HttpStatusCode.NotFound)
						{
							_logger.LogDebug("Read ref {RefName} -> None", name);
							return default;
						}
						else if (!response.IsSuccessStatusCode)
						{
							_logger.LogError("Unable to read ref {RefName} (status: {StatusCode}, body: {Body})", name, response.StatusCode, await response.Content.ReadAsStringAsync(cancellationToken));
							throw new StorageException($"Unable to read ref '{name}'", null);
						}
						else
						{
							response.EnsureSuccessStatusCode();
							ReadRefResponse? data = await response.Content.ReadFromJsonAsync<ReadRefResponse>(cancellationToken: cancellationToken);
							_logger.LogDebug("Read ref {RefName} -> {Blob}#{ExportIdx}", name, data!.Blob, data!.ExportIdx);
							return new NodeLocator(data!.Blob, data!.ExportIdx);
						}
					}
				}
			}
		}

		/// <inheritdoc/>
		public override async Task WriteRefTargetAsync(RefName name, NodeLocator target, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			_logger.LogDebug("Writing ref {RefName} -> {Blob}#{ExportIdx}", name, target.Blob, target.ExportIdx);
			using (HttpClient httpClient = _createClient())
			{
				using (HttpResponseMessage response = await httpClient.PutAsync($"refs/{name}", new { locator = target.Blob, exportIdx = target.ExportIdx, options }, cancellationToken))
				{
					response.EnsureSuccessStatusCode();
				}
			}
		}

		#endregion
	}
}
