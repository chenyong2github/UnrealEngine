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
	using BucketId = StringId<IBucket>;

	/// <summary>
	/// Response from trying to set a reference
	/// </summary>
	public class SetRefResponse
	{
		/// <summary>
		/// List of missing hashes
		/// </summary>
		public List<IoHash> Need { get; set; } = new List<IoHash>();
	}

	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class RefsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the namespace collection
		/// </summary>
		private readonly INamespaceCollection NamespaceCollection;

		/// <summary>
		/// Singleton instance of the bucket collection
		/// </summary>
		private readonly IBucketCollection BucketCollection;

		/// <summary>
		/// Singleton instance of the ref collection
		/// </summary>
		private readonly IRefCollection RefCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NamespaceCollection">The namespace collection</param>
		/// <param name="BucketCollection">The bucket collection</param>
		/// <param name="RefCollection">The ref collection</param>
		public RefsController(INamespaceCollection NamespaceCollection, IBucketCollection BucketCollection, IRefCollection RefCollection)
		{
			this.NamespaceCollection = NamespaceCollection;
			this.BucketCollection = BucketCollection;
			this.RefCollection = RefCollection;
		}

		/// <summary>
		/// Submit a structured object to the storage api. The object hash will be verified against the received payload. 
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="BucketId">Name of the bucket.</param>
		/// <param name="RefName">Name of the reference.</param>
		/// <param name="Data">The emcpdedata to add</param>
		/// <returns></returns>
		[HttpPut]
		[Route("/api/v1/refs/{NamespaceId}/{BucketId}/{RefName}")]
		public async Task<ActionResult<SetRefResponse>> SetRefAsync(NamespaceId NamespaceId, BucketId BucketId, string RefName, [FromBody] byte[] Data)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.WriteRefs))
			{
				return Forbid();
			}

			CbObject Object = new CbObject(Data);

			SetRefResponse Response = new SetRefResponse();
			Response.Need = await RefCollection.SetAsync(NamespaceId, BucketId, RefName, Object);
			return Response;
		}

		/// <summary>
		/// Submit a structured object to the storage api. The object hash will be verified against the received payload. 
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="BucketId">Name of the bucket.</param>
		/// <param name="RefName">Name of the reference.</param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/refs/{NsName}/{BktName}/{RefName}")]
		public async Task<ActionResult> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, string RefName)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadRefs))
			{
				return Forbid();
			}

			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, RefName);
			if (Ref == null)
			{
				return NotFound();
			}

			return Content(Ref.Value.ToJson(), new MediaTypeHeaderValue("application/json"));
		}

		/// <summary>
		/// Does ref exist, HTTP 200 returned if successful, otherwise a HTTP 404 if missing.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="BucketId">Name of the bucket.</param>
		/// <param name="RefName">Name of the reference.</param>
		/// <returns></returns>
		[HttpHead]
		[Route("/api/v1/refs/{NsName}/{BktName}/{RefName}")]
		[ProducesResponseType(200)]
		[ProducesResponseType(404)]
		public async Task<ActionResult> DoesRefExistAsync(NamespaceId NamespaceId, BucketId BucketId, string RefName)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.ReadRefs))
			{
				return Forbid();
			}
			if (await RefCollection.GetAsync(NamespaceId, BucketId, RefName) == null)
			{
				return NotFound();
			}
			return Ok();
		}

		/// <summary>
		/// Delete a reference from the store.
		/// </summary>
		/// <param name="NamespaceId">Namespace for the operation</param>
		/// <param name="BucketId">Name of the bucket.</param>
		/// <param name="RefName">Name of the reference.</param>
		/// <returns></returns>
		[HttpDelete]
		[Route("/api/v1/refs/{NamespaceId}/{BucketId}/{RefName}")]
		public async Task<ActionResult> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, string RefName)
		{
			if (!await NamespaceCollection.AuthorizeAsync(NamespaceId, User, AclAction.DeleteRefs))
			{
				return Forbid();
			}
			if (!await RefCollection.DeleteAsync(NamespaceId, BucketId, RefName))
			{
				return NotFound();
			}
			return Ok();
		}
	}
}
