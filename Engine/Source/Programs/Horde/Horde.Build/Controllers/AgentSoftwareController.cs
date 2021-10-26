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
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// Controller for the /api/v1/software endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class AgentSoftwareController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Singleton instance of the client service
		/// </summary>
		private readonly AgentSoftwareService AgentSoftwareService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="AgentSoftwareService">The client service</param>
		public AgentSoftwareController(AclService AclService, AgentSoftwareService AgentSoftwareService)
		{
			this.AclService = AclService;
			this.AgentSoftwareService = AgentSoftwareService;
		}

		/// <summary>
		/// Finds all uploaded software matching the given criteria
		/// </summary>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/agentsoftware")]
		[ProducesResponseType(typeof(List<GetAgentSoftwareChannelResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindSoftwareAsync([FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			List<IAgentSoftwareChannel> Results = await AgentSoftwareService.FindChannelsAsync();

			List<object> Responses = new List<object>();
			foreach (IAgentSoftwareChannel Result in Results)
			{
				Responses.Add(new GetAgentSoftwareChannelResponse(Result).ApplyFilter(Filter));
			}
			return Responses;
		}

		/// <summary>
		/// Finds all uploaded software matching the given criteria
		/// </summary>
		/// <param name="Name">Name of the channel to get</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/agentsoftware/{Name}")]
		[ProducesResponseType(typeof(GetAgentSoftwareChannelResponse), 200)]
		public async Task<ActionResult<object>> FindSoftwareAsync(string Name, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			IAgentSoftwareChannel? Channel = await AgentSoftwareService.GetChannelAsync(new AgentSoftwareChannelName(Name));
			if(Channel == null)
			{
				return NotFound();
			}

			return new GetAgentSoftwareChannelResponse(Channel).ApplyFilter(Filter);
		}

		/// <summary>
		/// Uploads a new agent zip file
		/// </summary>
		/// <param name="Name">Name of the channel to post to</param>
		/// <param name="File">Zip archive containing the new client software</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/agentsoftware/{Name}/zip")]
		public async Task<ActionResult> SetArchiveAsync(string Name, [FromForm] IFormFile File)
		{
			if (!await AclService.AuthorizeAsync(AclAction.UploadSoftware, User))
			{
				return Forbid();
			}

			byte[] Data;
			using (MemoryStream MemoryStream = new MemoryStream())
			{
				using (System.IO.Stream Stream = File.OpenReadStream())
				{
					await Stream.CopyToAsync(MemoryStream);
				}
				Data = MemoryStream.ToArray();
			}

			await AgentSoftwareService.SetArchiveAsync(new AgentSoftwareChannelName(Name), User.Identity?.Name, Data);
			return Ok();
		}

		/// <summary>
		/// Gets the zip file for a specific channel
		/// </summary>
		/// <param name="Name">Name of the channel</param>
		/// <returns>Http response</returns>
		[HttpGet]
		[Route("/api/v1/agentsoftware/{Name}/zip")]
		public async Task<ActionResult> GetArchiveAsync(string Name)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, User))
			{
				return Forbid();
			}

			byte[]? Data = await AgentSoftwareService.GetArchiveAsync(new AgentSoftwareChannelName(Name));
			if (Data == null)
			{
				return NotFound();
			}

			return new FileStreamResult(new MemoryStream(Data), new MediaTypeHeaderValue("application/octet-stream")) { FileDownloadName = $"HordeAgent.zip" };
			
		}
	}
}
