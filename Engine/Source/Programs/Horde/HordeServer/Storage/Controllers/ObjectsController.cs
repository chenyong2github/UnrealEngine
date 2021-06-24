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
	/// Format to return object data
	/// </summary>
	public enum ObjectFormat
	{
		/// <summary>
		/// Json output
		/// </summary>
		Json,
		
		/// <summary>
		/// Compact binary output
		/// </summary>
		CompactBinary,
	}

	/// <summary>
	/// Request for negotiating list of objects to fetch
	/// </summary>
	public class FindObjectDeltaRequest
	{
		/// <summary>
		/// List of objects that the client has
		/// </summary>
		public List<IoHash> Have { get; set; } = new List<IoHash>();

		/// <summary>
		/// List of objects that the client wants
		/// </summary>
		public List<IoHash> Want { get; set; } = new List<IoHash>();
	}

	/// <summary>
	/// Response listing the objects that the client needs
	/// </summary>
	public class FindObjectDeltaResponse
	{
		/// <summary>
		/// List of of objects that the client needs
		/// </summary>
		public List<IoHash> Need { get; set; } = new List<IoHash>();
	}

	/// <summary>
	/// Response for putting an object
	/// </summary>
	public class PutObjectResponse
	{
		/// <summary>
		/// List of missing hashes
		/// </summary>
		public IoHash Id { get; set; }
	}

	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ObjectsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the namespace collection
		/// </summary>
		private readonly INamespaceCollection NamespaceCollection;

		/// <summary>
		/// Singleton instance of the object collection
		/// </summary>
		private readonly IObjectCollection ObjectCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NamespaceCollection">The namespace collection</param>
		/// <param name="ObjectCollection">The object collection</param>
		public ObjectsController(INamespaceCollection NamespaceCollection, IObjectCollection ObjectCollection)
		{
			this.NamespaceCollection = NamespaceCollection;
			this.ObjectCollection = ObjectCollection;
		}

		/// <summary>
		/// Submit a structured object to the storage api. The object hash will be verified against the received payload. 
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob. Must match the submitted data.</param>
		/// <param name="Data">The data to add</param>
		/// <returns></returns>
		[HttpPut]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}")]
		[ProducesResponseType(typeof(PutObjectResponse), 200)]
		public async Task<ActionResult> PutObjectAsync(NamespaceId NamespaceId, IoHash Hash, [FromBody] byte[] Data)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.WriteBlobs))
			{
				return Forbid();
			}

			await ObjectCollection.AddAsync(NamespaceId, Hash, new CbObject(Data));
			return Ok(new PutObjectResponse { Id = Hash });
		}

		/// <summary>
		/// Finds deltas between two object sets
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">The root object hash</param>
		/// <param name="Request">Request object</param>
		/// <returns></returns>
		[HttpPost]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}/delta")]
		public Task<ActionResult<FindObjectDeltaResponse>> FindObjectDeltaAsync(NamespaceId NamespaceId, IoHash Hash, [FromBody] FindObjectDeltaRequest Request)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Does payload exist, HTTP 200 returned if successful, otherwise a HTTP 404 if missing..
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob to check</param>
		/// <returns></returns>
		[HttpHead]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}")]
		[ProducesResponseType(200)]
		[ProducesResponseType(404)]
		public async Task<ActionResult> DoesBlobExistAsync(NamespaceId NamespaceId, [FromRoute] IoHash Hash)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}
			if (!await ObjectCollection.ExistsAsync(NamespaceId, Hash))
			{
				return NotFound();
			}
			return Ok();
		}

		/// <summary>
		/// Returns the object with the given hash.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">Hash of the blob. Must match the submitted data.</param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}")]
		public async Task<ActionResult> GetObjectAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}

			CbObject? Object = await ObjectCollection.GetAsync(NamespaceId, Hash);
			if(Object == null)
			{
				return NotFound();
			}

			return Ok(Object.GetView().ToArray());
		}

		/// <summary>
		/// Send a list of query parameters called id for a batch lookup similar to the HEAD request.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hashes">List of ids to check</param>
		/// <returns></returns>
		[HttpPost]
		[Route("/api/v1/objects/{NamespaceId}/exists")]
		public async Task<ActionResult<ExistsResponse>> DoBlobsExistAsync(NamespaceId NamespaceId, [FromQuery(Name = "id")] IoHash[] Hashes)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}

			ExistsResponse Response = new ExistsResponse();
			Response.Id = (await ObjectCollection.ExistsAsync(NamespaceId, Hashes)).ToArray();
			return Response;
		}
	}
}
