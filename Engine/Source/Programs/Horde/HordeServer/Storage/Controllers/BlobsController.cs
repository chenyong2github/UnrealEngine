// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Net.Http.Headers;
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Models;
using MongoDB.Bson;
using HordeServer.Utilities;
using EpicGames.Core;
using HordeServer.Storage.Collections;
using EpicGames.Serialization;

namespace HordeServer.Storage.Controllers
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Response from putting a blob
	/// </summary>
	public class PutBlobResponse
	{
		/// <summary>
		/// Hash of the blob that was added
		/// </summary>
		[CbField("h")]
		public IoHash Hash { get; set; }
	}

	/// <summary>
	/// Response for queries to test whether a set of blobs exist
	/// </summary>
	public class ExistsResponse
	{
		/// <summary>
		/// List of blobs that exist
		/// </summary>
		[CbField("id")]
		public List<IoHash>? Id { get; set; } = null;
	}

	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class BlobsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the namespace collection
		/// </summary>
		private readonly INamespaceCollection NamespaceCollection;

		/// <summary>
		/// Singleton instance of the blob collection
		/// </summary>
		private readonly IBlobCollection BlobCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NamespaceCollection">The namespace collection</param>
		/// <param name="BlobCollection">The blob collection</param>
		public BlobsController(INamespaceCollection NamespaceCollection, IBlobCollection BlobCollection)
		{
			this.NamespaceCollection = NamespaceCollection;
			this.BlobCollection = BlobCollection;
		}

		/// <summary>
		/// Submit a blob to the storage api. The request body should be a raw byte stream containing the blob data.
		/// The payload hash will be verified against the received payload. The hash of the object is returned for a successful submission.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob. Must match the submitted data.</param>
		/// <returns></returns>
		[HttpPut]
		[Route("/api/v1/blobs/{NamespaceId}/{Hash}")]
		public async Task<ActionResult<PutBlobResponse>> PutBlobAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.WriteBlobs))
			{
				return Forbid();
			}

			byte[] Data;
			using (MemoryStream MemoryStream = new MemoryStream())
			{
				await Request.Body.CopyToAsync(MemoryStream);
				Data = MemoryStream.ToArray();
			}

			IoHash ActualHash = IoHash.Compute(Data);
			if (Hash != ActualHash)
			{
				return BadRequest("Incorrect hash");
			}

			using (MemoryStream MemoryStream = new MemoryStream(Data))
			{
				await BlobCollection.WriteStreamAsync(NamespaceId, Hash, MemoryStream);
			}

			return new PutBlobResponse { Hash = Hash };
		}

		/// <summary>
		/// Does payload exist, HTTP 200 returned if successful, otherwise a HTTP 404 if missing..
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob to check</param>
		/// <returns></returns>
		[HttpHead]
		[Route("/api/v1/blobs/{NamespaceId}/{Hash}")]
		[ProducesResponseType(200)]
		[ProducesResponseType(404)]
		public async Task<ActionResult> DoesBlobExistAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}
			if (!await BlobCollection.ExistsAsync(NamespaceId, Hash))
			{
				return NotFound();
			}

			return Ok();
		}

		/// <summary>
		/// Returns the payload with the given hash.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob. Must match the submitted data.</param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/blobs/{NamespaceId}/{Hash}")]
		public async Task<ActionResult> GetBlobAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}

			Stream? Stream = await BlobCollection.TryReadStreamAsync(NamespaceId, Hash);
			if (Stream == null)
			{
				return NotFound();
			}

			return new FileStreamResult(Stream, new MediaTypeHeaderValue("application/octet-stream"));
		}

		/// <summary>
		/// Send a list of query parameters called id for a batch lookup similar to the HEAD request.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hashes">List of ids to check</param>
		/// <returns></returns>
		[HttpPost]
		[Route("/api/v1/blobs/{NamespaceId}/exists")]
		public async Task<ActionResult<ExistsResponse>> DoBlobsExistAsync(NamespaceId NamespaceId, [FromQuery(Name = "id")] IoHash[] Hashes)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}

			ExistsResponse Exists = new ExistsResponse();
			Exists.Id = await BlobCollection.ExistsAsync(NamespaceId, Hashes);
			return Exists;
		}
	}
}
