// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Auth;
using EpicGames.Serialization;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Impl
{
	/// <summary>
	/// Response from adding a new object
	/// </summary>
	public class RefPutResponse
	{
		/// <summary>
		/// List of missing hashes
		/// </summary>
		public List<IoHash> Needs { get; set; } = new List<IoHash>();
	}

	/// <summary>
	/// Response from posting to the /exists endpoint
	/// </summary>
	public class RefExistsResponse
	{
		/// <summary>
		/// List of hashes that the blob store needs.
		/// </summary>
		public List<RefId> Needs { get; set; } = new List<RefId>();
	}

	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> using REST requests.
	/// </summary>
	public sealed class HttpStorageClient : IStorageClient
	{
		class RefImpl : IRef
		{
			public NamespaceId NamespaceId { get; set; }

			public BucketId BucketId { get; set; }

			public RefId RefId { get; set; }

			public CbObject Value { get; }

			public RefImpl(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value)
			{
				this.NamespaceId = NamespaceId;
				this.BucketId = BucketId;
				this.RefId = RefId;
				this.Value = Value;
			}
		}

		const string HashHeaderName = "X-Jupiter-IoHash";
		const string LastAccessHeaderName = "X-Jupiter-LastAccess";

		const string CompactBinaryMimeType = "application/x-ue-cb";

		HttpClient HttpClient;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="HttpClient">Http client for the blob store. Should be pre-configured with a base address and appropriate authentication headers.</param>
		public HttpStorageClient(HttpClient HttpClient)
		{
			this.HttpClient = HttpClient;
		}

		#region Blobs

		/// <inheritdoc/>
		public async Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/blobs/{NamespaceId}/{Hash}");

			HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			try
			{
				if (Response.StatusCode == System.Net.HttpStatusCode.NotFound)
				{
					throw new BlobNotFoundException(NamespaceId, Hash);
				}

				Response.EnsureSuccessStatusCode();
				return await Response.Content.ReadAsStreamAsync();
			}
			catch
			{
				Response.Dispose();
				throw;
			}
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Put, $"api/v1/blobs/{NamespaceId}/{Hash}");

			StreamContent Content = new StreamContent(Stream);
			Content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
			Request.Content = Content;

			HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			Response.EnsureSuccessStatusCode();
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public async Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}");

			Request.Headers.Accept.Clear();
			Request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CompactBinaryMimeType));

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request);
			if (!Response.IsSuccessStatusCode)
			{
				if (Response.StatusCode == System.Net.HttpStatusCode.NotFound)
				{
					throw new RefNotFoundException(NamespaceId, BucketId, RefId);
				}
				else
				{
					throw new RefException(NamespaceId, BucketId, RefId, await GetMessageFromResponse(Response));
				}
			}

			byte[] Data = await Response.Content.ReadAsByteArrayAsync();
			return new RefImpl(NamespaceId, BucketId, RefId, new CbObject(Data));
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> TrySetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value, CancellationToken CancellationToken)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Value.GetView());

			StreamContent Content = new StreamContent(Stream);
			Content.Headers.ContentType = new MediaTypeHeaderValue(CompactBinaryMimeType);
			Content.Headers.Add(HashHeaderName, IoHash.Compute(Value.GetView().Span).ToString());

			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Put, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}");

			Request.Headers.Accept.Clear();
			Request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
			Request.Content = Content;

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request);
			if (!Response.IsSuccessStatusCode)
			{
				throw new RefException(NamespaceId, BucketId, RefId, await GetMessageFromResponse(Response));
			}

			RefPutResponse? ResponseBody = await ReadJsonResponse<RefPutResponse>(Response.Content);
			if (ResponseBody == null)
			{
				throw new RefException(NamespaceId, BucketId, RefId, "Unable to parse response body");
			}

			return ResponseBody.Needs;
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash Hash, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}/finalize/{Hash}");

			Request.Headers.Accept.Clear();
			Request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request);
			if (!Response.IsSuccessStatusCode)
			{
				throw new RefException(NamespaceId, BucketId, RefId, await GetMessageFromResponse(Response));
			}

			RefPutResponse? ResponseBody = await ReadJsonResponse<RefPutResponse>(Response.Content);
			if (ResponseBody == null)
			{
				throw new RefException(NamespaceId, BucketId, RefId, "Unable to parse response body");
			}

			return ResponseBody.Needs;
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Delete, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}");

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			return Response.IsSuccessStatusCode;
		}

		/// <inheritdoc/>
		public async Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Head, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}");

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			if (Response.IsSuccessStatusCode)
			{
				return true;
			}
			else if (Response.StatusCode == System.Net.HttpStatusCode.NotFound)
			{
				return false;
			}
			else
			{
				throw new RefException(NamespaceId, BucketId, RefId, $"Unexpected response for {nameof(HasRefAsync)} call: {Response.StatusCode}");
			}
		}

		/// <inheritdoc/>
		public async Task<List<RefId>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<RefId> RefIds, CancellationToken CancellationToken)
		{
			string RefList = String.Join("&", RefIds.Select(x => $"id={x}"));

			using HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"/api/v1/refs/{NamespaceId}/{BucketId}/exists?{RefList}");

			using HttpResponseMessage Response = await HttpClient.SendAsync(Request, CancellationToken);
			if (!Response.IsSuccessStatusCode)
			{
				throw new RefException(NamespaceId, BucketId, new RefId(IoHash.Zero), await GetMessageFromResponse(Response));
			}

			RefExistsResponse? ResponseContent = await ReadJsonResponse<RefExistsResponse>(Response.Content);
			if (ResponseContent == null)
			{
				throw new RefException(NamespaceId, BucketId, new RefId(IoHash.Zero), "Unable to parse response body");
			}

			return ResponseContent.Needs;
		}

		#endregion

		class ProblemDetails
		{
			public string? Title { get; set; }
		}

		static async Task<string> GetMessageFromResponse(HttpResponseMessage Response)
		{
			StringBuilder Message = new StringBuilder($"HTTP {Response.StatusCode}");
			try
			{
				byte[] Data = await Response.Content.ReadAsByteArrayAsync();
				ProblemDetails? Details = JsonSerializer.Deserialize<ProblemDetails>(Data);
				Message.Append($": {Details?.Title ?? "No description available"}");
			}
			catch
			{
				Message.Append(" (Unable to parse response data)");
			}
			return Message.ToString();
		}

		static async Task<T> ReadJsonResponse<T>(HttpContent Content)
		{
			byte[] Data = await Content.ReadAsByteArrayAsync();
			return JsonSerializer.Deserialize<T>(Data, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
		}
	}
}
