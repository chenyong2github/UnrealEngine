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
using System.Net;
using System.Net.Mime;

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
		[CbField("h")]
		public List<CbObjectAttachment> Have { get; set; } = new List<CbObjectAttachment>();
	}

	/// <summary>
	/// Response listing the objects that the client needs
	/// </summary>
	public class FindObjectDeltaResponse
	{
		/// <summary>
		/// List of of objects that the client needs
		/// </summary>
		[CbField("n")]
		public List<CbObjectAttachment> Need { get; set; } = new List<CbObjectAttachment>();
	}

	/// <summary>
	/// Request for retrieving a tree of objects
	/// </summary>
	public class GetObjectTreeRequest
	{
		/// <summary>
		/// List of objects that the client already has
		/// </summary>
		[CbField("h")]
		public List<CbObjectAttachment> Have { get; set; } = new List<CbObjectAttachment>();
	}

	/// <summary>
	/// Response for putting an object
	/// </summary>
	public class PutObjectResponse
	{
		/// <summary>
		/// List of missing hashes
		/// </summary>
		[CbField("id")]
		public CbObjectAttachment Id { get; set; }
	}

	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[FormatFilter]
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
		public async Task<ActionResult> PutObjectAsync(NamespaceId NamespaceId, IoHash Hash, [FromBody] CbObject Data)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.WriteBlobs))
			{
				return Forbid();
			}

			await ObjectCollection.AddAsync(NamespaceId, Hash, Data);
			return Ok(new PutObjectResponse { Id = Hash });
		}

		/// <summary>
		/// Finds deltas between two object sets
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">The root object hash</param>
		/// <param name="Request">Request object</param>
		/// <param name="Depth">Maximum depth in the tree to scan</param>
		/// <param name="Count">Maximum number of items to return</param>
		/// <returns></returns>
		[HttpPost]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}/delta")]
		public async Task<ActionResult<FindObjectDeltaResponse>> FindObjectDeltaAsync(NamespaceId NamespaceId, IoHash Hash, [FromBody] FindObjectDeltaRequest Request, int Depth = -1, int Count = -1)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				return Forbid();
			}

			CbObject? Object = await ObjectCollection.GetAsync(NamespaceId, Hash);
			if (Object == null)
			{
				return NotFound();
			}

			FindObjectDeltaResponse Response = new FindObjectDeltaResponse();

			HashSet<IoHash> SeenHashes = new HashSet<IoHash>(Request.Have.Select(x => x.Hash));
			if (!SeenHashes.Contains(Hash))
			{
				SeenHashes.Add(IoHash.Zero); // Don't return empty fields

				Queue<(IoHash, int)> Queue = new Queue<(IoHash, int)>();
				Queue.Enqueue((Hash, Depth));

				while (Queue.Count > 0 && (Count == -1 || Response.Need.Count < Count))
				{
					(IoHash CurrentHash, int CurrentDepth) = Queue.Dequeue();
					Response.Need.Add(CurrentHash);

					if (CurrentDepth != 0)
					{
						CbObject? CurrentObject = await ObjectCollection.GetAsync(NamespaceId, CurrentHash);
						if (CurrentObject != null)
						{
							int NextDepth = (CurrentDepth == -1) ? -1 : (CurrentDepth - 1);
							CurrentObject.IterateAttachments(Field =>
							{
								if (Field.IsObjectAttachment())
								{
									CbObjectAttachment Attachment = Field.AsObjectAttachment();
									if (SeenHashes.Add(Field.AsObjectAttachment()))
									{
										Queue.Enqueue((Field.AsObjectAttachment().Hash, NextDepth));
									}
								}
							});
						}
					}
				}
			}

			return Response;
		}

		/// <summary>
		/// Gets an object tree underneath a given root
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">The root object hash</param>
		[HttpGet]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}/tree")]
		public Task GetObjectTreeAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			return GetObjectTreeAsync(NamespaceId, Hash, new GetObjectTreeRequest());
		}

		/// <summary>
		/// Gets an object tree underneath a given root
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="Hash">The root object hash</param>
		/// <param name="Request">Request object</param>
		[HttpPost]
		[Route("/api/v1/objects/{NamespaceId}/{Hash}/tree")]
		public async Task GetObjectTreeAsync(NamespaceId NamespaceId, IoHash Hash, [FromBody] GetObjectTreeRequest Request)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadBlobs))
			{
				Response.StatusCode = (int)HttpStatusCode.Forbidden;
				return;
			}

			CbObject? Object = await ObjectCollection.GetAsync(NamespaceId, Hash);
			if (Object == null)
			{
				Response.StatusCode = (int)HttpStatusCode.NotFound;
				return;
			}

			Response.StatusCode = 200;
			await Response.StartAsync(HttpContext.RequestAborted);

			HashSet<IoHash> HaveHashes = new HashSet<IoHash>(Request.Have.Select(x => x.Hash));
			if (!HaveHashes.Contains(Hash))
			{
				List<IoHash> RequestHashes = new List<IoHash>();
				for (; ; )
				{
					CbWriter Writer = new CbWriter();
					Writer.WriteObjectAttachmentValue(Hash);
					await Response.BodyWriter.WriteAsync(Writer.ToByteArray());

					if (Object != null)
					{
						await Response.BodyWriter.WriteAsync(Object.GetView());

						List<CbBinaryAttachment> BinaryAttachments = new List<CbBinaryAttachment>();
						Object.IterateAttachments(Field =>
						{
							if (Field.IsBinaryAttachment())
							{
								BinaryAttachments.Add(Field.AsBinaryAttachment());
							}
							else if (Field.IsObjectAttachment() && HaveHashes.Add(Field.AsObjectAttachment()))
							{
								RequestHashes.Add(Field.AsObjectAttachment());
							}
						});

						foreach (CbBinaryAttachment BinaryAttachment in BinaryAttachments)
						{
							CbWriter BinaryWriter = new CbWriter();
							BinaryWriter.WriteBinaryAttachmentValue(BinaryAttachment);
							await Response.BodyWriter.WriteAsync(BinaryWriter.ToByteArray());
						}
					}

					if (RequestHashes.Count == 0)
					{
						break;
					}

					Hash = RequestHashes[RequestHashes.Count - 1];
					RequestHashes.RemoveAt(RequestHashes.Count - 1);

					Object = await ObjectCollection.GetAsync(NamespaceId, Hash);
				}
			}
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
		[Route("/api/v1/objects/{NamespaceId}/{Hash}.{format?}")]
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

			return Ok(Object);
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
			Response.Id = await ObjectCollection.ExistsAsync(NamespaceId, Hashes);
			return Response;
		}
	}
}
