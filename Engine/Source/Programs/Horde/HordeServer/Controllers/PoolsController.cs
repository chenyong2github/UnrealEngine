// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Controller for the /api/v1/pools endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class PoolsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Singleton instance of the pool service
		/// </summary>
		private readonly PoolService PoolService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="PoolService">The pool service</param>
		public PoolsController(AclService AclService, PoolService PoolService)
		{
			this.AclService = AclService;
			this.PoolService = PoolService;
		}

		/// <summary>
		/// Creates a new pool
		/// </summary>
		/// <param name="Create">Parameters for the new pool.</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/pools")]
		public async Task<ActionResult<CreatePoolResponse>> CreatePoolAsync([FromBody] CreatePoolRequest Create)
		{
			if(!await AclService.AuthorizeAsync(AclAction.CreatePool, User))
			{
				return Forbid();
			}

			IPool NewPool = await PoolService.CreatePoolAsync(Create.Name, Create.Condition, Create.EnableAutoscaling, Create.MinAgents, Create.NumReserveAgents, Create.Properties);
			return new CreatePoolResponse(NewPool.Id.ToString());
		}

		/// <summary>
		/// Query all the pools
		/// </summary>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the pools</returns>
		[HttpGet]
		[Route("/api/v1/pools")]
		[ProducesResponseType(typeof(List<GetPoolResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetPoolsAsync([FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.ListPools, User))
			{
				return Forbid();
			}

			List<IPool> Pools = await PoolService.GetPoolsAsync();

			List<object> Responses = new List<object>();
			foreach (IPool Pool in Pools)
			{
				Responses.Add(new GetPoolResponse(Pool).ApplyFilter(Filter));
			}
			return Responses;
		}

		/// <summary>
		/// Retrieve information about a specific pool
		/// </summary>
		/// <param name="PoolId">Id of the pool to get information about</param>
		/// <param name="Filter">Filter to apply to the returned properties</param>
		/// <returns>Information about the requested pool</returns>
		[HttpGet]
		[Route("/api/v1/pools/{PoolId}")]
		[ProducesResponseType(typeof(GetPoolResponse), 200)]
		public async Task<ActionResult<object>> GetPoolAsync(string PoolId, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.ViewPool, User))
			{
				return Forbid();
			}

			PoolId PoolIdValue = new PoolId(PoolId);

			IPool? Pool = await PoolService.GetPoolAsync(PoolIdValue);
			if (Pool == null)
			{
				return NotFound();
			}

			return new GetPoolResponse(Pool).ApplyFilter(Filter);
		}

		/// <summary>
		/// Update a pool's properties.
		/// </summary>
		/// <param name="PoolId">Id of the pool to update</param>
		/// <param name="Update">Items on the pool to update</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/pools/{PoolId}")]
		public async Task<ActionResult> UpdatePoolAsync(string PoolId, [FromBody] UpdatePoolRequest Update)
		{
			if (!await AclService.AuthorizeAsync(AclAction.UpdatePool, User))
			{
				return Forbid();
			}

			PoolId PoolIdValue = new PoolId(PoolId);
			
			IPool? Pool = await PoolService.GetPoolAsync(PoolIdValue);
			if(Pool == null)
			{
				return NotFound();
			}

			await PoolService.UpdatePoolAsync(Pool, Update.Name, Update.Condition, Update.EnableAutoscaling, Update.MinAgents, Update.NumReserveAgents, Update.Properties);
			return new OkResult();
		}

		/// <summary>
		/// Delete a pool
		/// </summary>
		/// <param name="PoolId">Id of the pool to delete</param>
		/// <returns>Http result code</returns>
		[HttpDelete]
		[Route("/api/v1/pools/{PoolId}")]
		public async Task<ActionResult> DeletePoolAsync(string PoolId)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DeletePool, User))
			{
				return Forbid();
			}

			PoolId PoolIdValue = new PoolId(PoolId);
			if(!await PoolService.DeletePoolAsync(PoolIdValue))
			{
				return NotFound();
			}
			return new OkResult();
		}

		/// <summary>
		/// Batch update pool properties
		/// </summary>
		/// <param name="BatchUpdates">List of pools to update</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/pools")]
		public async Task<ActionResult> UpdatePoolAsync([FromBody] List<BatchUpdatePoolRequest> BatchUpdates)
		{
			if (!await AclService.AuthorizeAsync(AclAction.UpdatePool, User))
			{
				return Forbid();
			}

			foreach (BatchUpdatePoolRequest Update in BatchUpdates)
			{
				PoolId PoolIdValue = new PoolId(Update.Id);

				IPool? Pool = await PoolService.GetPoolAsync(PoolIdValue);
				if (Pool == null)
				{
					return NotFound();
				}

				await PoolService.UpdatePoolAsync(Pool, Update.Name, NewProperties: Update.Properties);
			}
			return new OkResult();
		}
	}
}
