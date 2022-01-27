// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Google.Protobuf;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
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
		public List<IoHash> Needs { get; set; } = new List<IoHash>();
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

			public IoHash RefId { get; set; }

			public CbObject Value { get; }

			public RefImpl(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, CbObject Value)
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
		public async Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			using HttpResponseMessage Response = await HttpClient.GetAsync($"/api/v1/blobs/{NamespaceId}/{Hash}");
			return await Response.Content.ReadAsStreamAsync();
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream)
		{
			StreamContent Content = new StreamContent(Stream);
			Content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");

			HttpResponseMessage Response = await HttpClient.PutAsync($"api/v1/blobs/{NamespaceId}/{Hash}", Content);
			Response.EnsureSuccessStatusCode();
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public async Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId)
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
		public async Task<List<IoHash>> SetRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, CbObject Value)
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
		public async Task<List<IoHash>> FinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, IoHash Hash)
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
		public async Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId)
		{
			using HttpResponseMessage Response = await HttpClient.DeleteAsync($"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}");
			return Response.IsSuccessStatusCode;
		}

		/// <inheritdoc/>
		public async Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId)
		{
			using (HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Head, $"api/v1/refs/{NamespaceId}/{BucketId}/{RefId}"))
			{
				using HttpResponseMessage Response = await HttpClient.SendAsync(Request);
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
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<IoHash> RefIds)
		{
			string RefList = String.Join("&", RefIds.Select(x => $"id={x}"));

			using HttpResponseMessage Response = await HttpClient.PostAsync($"/api/v1/refs/{NamespaceId}/{BucketId}/exists?{RefList}", null);
			if (!Response.IsSuccessStatusCode)
			{
				throw new RefException(NamespaceId, BucketId, IoHash.Zero, await GetMessageFromResponse(Response));
			}

			RefExistsResponse? ResponseContent = await ReadJsonResponse<RefExistsResponse>(Response.Content);
			if (ResponseContent == null)
			{
				throw new RefException(NamespaceId, BucketId, IoHash.Zero, "Unable to parse response body");
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
