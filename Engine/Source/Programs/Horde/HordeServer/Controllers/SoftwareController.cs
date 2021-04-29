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

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class SoftwareController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Singleton instance of the client service
		/// </summary>
		private readonly SoftwareService SoftwareService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="SoftwareService">The client service</param>
		public SoftwareController(AclService AclService, SoftwareService SoftwareService)
		{
			this.AclService = AclService;
			this.SoftwareService = SoftwareService;
		}

		/// <summary>
		/// Uploads a new agent zip file
		/// </summary>
		/// <param name="File">Zip archive containing the new client software</param>
		/// <param name="Default">Whether the client should immediately be made the default</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/software")]
		public async Task<ActionResult<CreateSoftwareResponse>> CreateSoftwareAsync([FromForm] IFormFile File, [FromForm] bool Default = false)
		{
			if (!await AclService.AuthorizeAsync(AclAction.UploadSoftware, User))
			{
				return Forbid();
			}

			ObjectId Id;
			using (System.IO.Stream Stream = File.OpenReadStream())
			{
				Id = await SoftwareService.CreateSoftwareAsync(Stream, User.Identity.Name, Default);
			}
			return new CreateSoftwareResponse(Id.ToString());
		}

		/// <summary>
		/// Finds all uploaded software matching the given criteria
		/// </summary>
		/// <param name="CreatedByUser">The user that created the software</param>
		/// <param name="MadeDefaultByUser">The user that made it the default</param>
		/// <param name="MadeDefault">Whether the software was made the default</param>
		/// <param name="Offset">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/software")]
		[ProducesResponseType(typeof(List<GetSoftwareResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindSoftwareAsync([FromQuery] string? CreatedByUser = null, [FromQuery] string? MadeDefaultByUser = null, [FromQuery] bool? MadeDefault = null, [FromQuery] PropertyFilter? Filter = null, [FromQuery] int Offset = 0, [FromQuery] int Count = 200)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			List<ISoftware> Results = await SoftwareService.FindSoftwareAsync(CreatedByUser, MadeDefaultByUser, MadeDefault, Offset, Count);

			List<object> Responses = new List<object>();
			foreach (ISoftware Result in Results)
			{
				Responses.Add(new GetSoftwareResponse(Result).ApplyFilter(Filter));
			}
			return Responses;
		}

		/// <summary>
		/// Gets information about a specific client revision
		/// </summary>
		/// <param name="Id">Unique id of the client software</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/software/{Id}")]
		[ProducesResponseType(typeof(GetSoftwareResponse), 200)]
		public async Task<ActionResult<object>> GetSoftwareAsync(string Id, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			ISoftware? Client = await GetSoftwareInternalAsync(Id, false);
			if (Client == null)
			{
				return NotFound();
			}

			return new GetSoftwareResponse(Client).ApplyFilter(Filter);
		}

		/// <summary>
		/// Gets the zip file for a specific client revision
		/// </summary>
		/// <param name="Id">Unique id of the client</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/software/{Id}/download")]
		public async Task<ActionResult<GetSoftwareResponse>> DownloadSoftwareAsync(string Id)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			ISoftware? Software = await GetSoftwareInternalAsync(Id, true);
			if (Software == null)
			{
				return NotFound();
			}

			return new FileStreamResult(new MemoryStream(Software.Data!), new MediaTypeHeaderValue("application/octet-stream"));
		}

		/// <summary>
		/// Find the software matching the given identifier
		/// </summary>
		/// <param name="Id">The identifier to match. May be an object id or "latest"</param>
		/// <param name="bIncludeData">Whether to include data in the response</param>
		/// <returns>Software interface</returns>
		async Task<ISoftware?> GetSoftwareInternalAsync(string Id, bool bIncludeData)
		{
			if (Id.Equals("latest", StringComparison.OrdinalIgnoreCase))
			{
				return await SoftwareService.GetDefaultSoftwareAsync(bIncludeData);
			}
			else
			{
				return await SoftwareService.GetSoftwareAsync(Id.ToObjectId(), bIncludeData);
			}
		}

		/// <summary>
		/// Deletes a version of the client
		/// </summary>
		/// <param name="Id">Unique id of the client to delete</param>
		/// <returns>Http response</returns>
		[HttpDelete]
		[Route("/api/v1/software/{Id}")]
		public async Task<ActionResult> DeleteSoftwareAsync(string Id)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DeleteSoftware, User))
			{
				return Forbid();
			}
			await SoftwareService.DeleteSoftwareAsync(Id.ToObjectId());
			return new OkResult();
		}
	}
}
