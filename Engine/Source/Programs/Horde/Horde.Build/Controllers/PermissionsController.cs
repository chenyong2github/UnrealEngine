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
using System.Linq.Expressions;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/permissions endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class PermissionsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		public PermissionsController(AclService AclService)
		{
			this.AclService = AclService;
		}

		/// <summary>
		/// Gets a scope ACL
		/// </summary>
		/// <returns>The ACL information</returns>
		[HttpGet]
		[Route("/api/v1/permissions")]
		public async Task<ActionResult<GetAclResponse>> GetPermissions()
		{
			GlobalPermissionsCache PermissionsCache = new GlobalPermissionsCache();
			if (!await AclService.AuthorizeAsync(AclAction.ViewPermissions, User, PermissionsCache))
			{
				return Forbid();
			}

			GlobalPermissions GlobalPermissions = PermissionsCache.GlobalPermissions ?? await AclService.GetGlobalPermissionsAsync();
			return new GetAclResponse(GlobalPermissions.Acl);
		}

		/// <summary>
		/// Updates a scope ACL
		/// </summary>
		/// <param name="Update">The update request</param>
		[HttpPut]
		[Route("/api/v1/permissions")]
		public async Task<ActionResult> UpdateScopeAcl(UpdateAclRequest Update)
		{
			for (; ; )
			{
				GlobalPermissionsCache PermissionsCache = new GlobalPermissionsCache();
				if (!await AclService.AuthorizeAsync(AclAction.ChangePermissions, User, PermissionsCache))
				{
					return Forbid();
				}

				GlobalPermissions GlobalPermissions = PermissionsCache.GlobalPermissions ?? await AclService.GetGlobalPermissionsAsync();
				GlobalPermissions.Acl = Acl.Merge(GlobalPermissions.Acl, Update) ?? new Acl();

				if (await AclService.TryUpdateGlobalPermissionsAsync(GlobalPermissions))
				{
					return Ok();
				}
			}
		}
	}
}
