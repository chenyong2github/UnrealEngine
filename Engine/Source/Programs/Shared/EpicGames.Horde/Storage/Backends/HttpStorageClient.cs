// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage.Backends
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which communicates with an upstream Horde instance via HTTP.
	/// </summary>
	public class HttpStorageClient : StorageClientBase
	{
		class WriteBlobResponse
		{
			public BlobLocator Locator { get; set; }
		}

		class ReadRefResponse
		{
			public BlobLocator Blob { get; set; }
			public int ExportIdx { get; set; }
		}

		readonly NamespaceId _namespaceId;
		readonly HttpClient _httpClient;

		/// <summary>
		/// Constructor
		/// </summary>
		public HttpStorageClient(NamespaceId namespaceId, HttpClient httpClient, IMemoryCache cache, ILogger logger) 
			: base(cache, logger)
		{
			_namespaceId = namespaceId;
			_httpClient = httpClient;
		}

		#region Blobs

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/storage/{_namespaceId}/blobs/{locator}"))
			{
				HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
				response.EnsureSuccessStatusCode();
				return await response.Content.ReadAsStreamAsync(cancellationToken);
			}
		}

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/storage/{_namespaceId}/blobs/{locator}?offset={offset}&length={length}"))
			{
				HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
				response.EnsureSuccessStatusCode();
				return await response.Content.ReadAsStreamAsync(cancellationToken);
			}
		}

		/// <inheritdoc/>
		public override async Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/storage/{_namespaceId}/blobs"))
			{
				using StreamContent streamContent = new StreamContent(stream);

				MultipartFormDataContent form = new MultipartFormDataContent();
				form.Add(streamContent, "file", "filename");

				request.Content = form;
				using (HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken))
				{
					response.EnsureSuccessStatusCode();
					WriteBlobResponse? data = await response.Content.ReadFromJsonAsync<WriteBlobResponse>(cancellationToken: cancellationToken);
					return data!.Locator;
				}
			}
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override async Task DeleteRefAsync(RefName name, CancellationToken cancellationToken)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Delete, $"api/v1/storage/{_namespaceId}/refs/{name}"))
			{
				using (HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken))
				{
					response.EnsureSuccessStatusCode();
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/storage/{_namespaceId}/refs/{name}"))
			{
				using (HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken))
				{
					response.EnsureSuccessStatusCode();
					ReadRefResponse? data = await response.Content.ReadFromJsonAsync<ReadRefResponse>(cancellationToken: cancellationToken);
					return new NodeLocator(data!.Blob, data!.ExportIdx);
				}
			}
		}

		/// <inheritdoc/>
		public override async Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default)
		{
			using (HttpResponseMessage response = await _httpClient.PutAsync($"api/v1/storage/{_namespaceId}/refs/{name}", new { locator = target.Blob, exportIdx = target.ExportIdx }, cancellationToken))
			{
				response.EnsureSuccessStatusCode();
			}
		}

		#endregion
	}
}
