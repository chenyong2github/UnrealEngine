// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authentication.OAuth.Claims;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Linq.Expressions;
using System.Security.Claims;
using System.Security.Cryptography;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Cache of global ACL
	/// </summary>
	public class GlobalPermissionsCache
	{
		/// <summary>
		/// The global permissions list
		/// </summary>
		public GlobalPermissions? GlobalPermissions { get; set; }
	}

	/// <summary>
	/// Wraps functionality for manipulating permissions
	/// </summary>
	public class AclService
	{
		/// <summary>
		/// Name of the role that can be used to administer agents
		/// </summary>
		public static AclClaim AgentRegistrationClaim { get; } = new AclClaim(HordeClaimTypes.Role, "agent-registration");

		/// <summary>
		/// Name of the role used to upload software
		/// </summary>
		public static AclClaim DownloadSoftwareClaim { get; } = new AclClaim(HordeClaimTypes.Role, "download-software");

		/// <summary>
		/// Name of the role used to upload software
		/// </summary>
		public static AclClaim UploadSoftwareClaim { get; } = new AclClaim(HordeClaimTypes.Role, "upload-software");

		/// <summary>
		/// Name of the role used to upload software
		/// </summary>
		public static AclClaim ConfigureProjectsClaim { get; } = new AclClaim(HordeClaimTypes.Role, "configure-projects");

		/// <summary>
		/// Name of the role used to upload software
		/// </summary>
		public static AclClaim StartChainedJobClaim { get; } = new AclClaim(HordeClaimTypes.Role, "start-chained-job");

		/// <summary>
		/// Role for all agents
		/// </summary>
		public static AclClaim AgentClaim { get; } = new AclClaim(HordeClaimTypes.Role, "agent");

		/// <summary>
		/// The default permissions
		/// </summary>
		Acl DefaultAcl = new Acl();

		/// <summary>
		/// The database service
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// The global permissions singleton
		/// </summary>
		ISingletonDocument<GlobalPermissions> GlobalPermissions;

		/// <summary>
		/// The settings object
		/// </summary>
		ServerSettings Settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="GlobalPermissions">The global permissions instance</param>
		/// <param name="Settings">The settings object</param>
		public AclService(DatabaseService DatabaseService, ISingletonDocument<GlobalPermissions> GlobalPermissions, IOptionsMonitor<ServerSettings> Settings)
		{
			this.DatabaseService = DatabaseService;
			this.GlobalPermissions = GlobalPermissions;
			this.Settings = Settings.CurrentValue;

			List<AclAction> AdminActions = new List<AclAction>();
			foreach (AclAction? Action in Enum.GetValues(typeof(AclAction)))
			{
				AdminActions.Add(Action!.Value);
			}

			DefaultAcl = new Acl();
			DefaultAcl.Entries.Add(new AclEntry(new AclClaim(ClaimTypes.Role, "internal:AgentRegistration"), new[] { AclAction.CreateAgent, AclAction.CreateSession }));
			DefaultAcl.Entries.Add(new AclEntry(AgentRegistrationClaim, new[] { AclAction.CreateAgent, AclAction.CreateSession, AclAction.UpdateAgent, AclAction.DownloadSoftware }));
			DefaultAcl.Entries.Add(new AclEntry(AgentClaim, new[] { AclAction.ViewProject, AclAction.ViewStream, AclAction.CreateEvent, AclAction.DownloadSoftware }));
			DefaultAcl.Entries.Add(new AclEntry(DownloadSoftwareClaim, new[] { AclAction.DownloadSoftware }));
			DefaultAcl.Entries.Add(new AclEntry(UploadSoftwareClaim, new[] { AclAction.UploadSoftware }));
			DefaultAcl.Entries.Add(new AclEntry(ConfigureProjectsClaim, new[] { AclAction.CreateProject, AclAction.UpdateProject, AclAction.ViewProject, AclAction.CreateStream, AclAction.UpdateStream, AclAction.ViewStream, AclAction.ChangePermissions }));
			DefaultAcl.Entries.Add(new AclEntry(StartChainedJobClaim, new[] { AclAction.CreateJob, AclAction.ExecuteJob, AclAction.UpdateJob, AclAction.ViewJob, AclAction.ViewTemplate, AclAction.ViewStream }));

			ServerSettings SettingsValue = Settings.CurrentValue;
			if (SettingsValue.AdminClaimType != null && SettingsValue.AdminClaimValue != null)
			{
				DefaultAcl.Entries.Add(new AclEntry(new AclClaim(SettingsValue.AdminClaimType, SettingsValue.AdminClaimValue), AdminActions.ToArray()));
			}
		}

		/// <summary>
		/// Gets the root ACL scope table
		/// </summary>
		/// <returns>Scopes instance</returns>
		public Task<GlobalPermissions> GetGlobalPermissionsAsync()
		{
			return GlobalPermissions.GetAsync();
		}

		/// <summary>
		/// Updates the root ACL scopes
		/// </summary>
		/// <param name="NewPermissions">New permissions</param>
		/// <returns>Async task</returns>
		public Task<bool> TryUpdateGlobalPermissionsAsync(GlobalPermissions NewPermissions)
		{
			return GlobalPermissions.TryUpdateAsync(NewPermissions);
		}

		/// <summary>
		/// Authorizes a user against a given scope
		/// </summary>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to validate</param>
		/// <param name="Cache">The ACL scope cache</param>
		/// <returns>Async task</returns>
		public async Task<bool> AuthorizeAsync(AclAction Action, ClaimsPrincipal User, GlobalPermissionsCache? Cache = null)
		{
			GlobalPermissions? GlobalPermissions;
			if(Cache == null)
			{
				GlobalPermissions = await GetGlobalPermissionsAsync();
			}
			else if(Cache.GlobalPermissions == null)
			{
				GlobalPermissions = Cache.GlobalPermissions = await GetGlobalPermissionsAsync();
			}
			else
			{
				GlobalPermissions = Cache.GlobalPermissions;
			}
			return GlobalPermissions.Acl.Authorize(Action, User) ?? DefaultAcl.Authorize(Action, User) ?? false;
		}

		/// <summary>
		/// Issues a bearer token with the given roles
		/// </summary>
		/// <param name="Claims">List of claims to include</param>
		/// <param name="Expiry">Time that the token expires</param>
		/// <returns>JWT security token with a claim for creating new agents</returns>
		public string IssueBearerToken(IEnumerable<AclClaim> Claims, TimeSpan? Expiry)
		{
			return IssueBearerToken(Claims.Select(x => new Claim(x.Type, x.Value)), Expiry);
		}

		/// <summary>
		/// Issues a bearer token with the given claims
		/// </summary>
		/// <param name="Claims">List of claims to include</param>
		/// <param name="Expiry">Time that the token expires</param>
		/// <returns>JWT security token with a claim for creating new agents</returns>
		public string IssueBearerToken(IEnumerable<Claim> Claims, TimeSpan? Expiry)
		{
			SigningCredentials SigningCredentials = new SigningCredentials(DatabaseService.JwtSigningKey, SecurityAlgorithms.HmacSha256);

			JwtSecurityToken Token = new JwtSecurityToken(DatabaseService.JwtIssuer, null, Claims, null, DateTime.UtcNow + Expiry, SigningCredentials);
			return new JwtSecurityTokenHandler().WriteToken(Token);
		}

		/// <summary>
		/// Get the roles for the given user
		/// </summary>
		/// <param name="User">The user to query roles for</param>
		/// <returns>Collection of roles</returns>
		public static HashSet<string> GetRoles(ClaimsPrincipal User)
		{
			return new HashSet<string>(User.Claims.Where(x => x.Type == ClaimTypes.Role).Select(x => x.Value));
		}

		/// <summary>
		/// Gets the user name from the given principal
		/// </summary>
		/// <param name="User">The principal to check</param>
		/// <returns></returns>
		public static string? GetUserName(ClaimsPrincipal User)
		{
			return (User.Claims.FirstOrDefault(x => x.Type == HordeClaimTypes.User) ?? User.Claims.FirstOrDefault(x => x.Type == ClaimTypes.Name))?.Value ?? "Anonymous";
		}

		/// <summary>
		/// Gets the role for a specific user
		/// </summary>
		/// <param name="SessionId">The session id</param>
		/// <returns>New claim instance</returns>
		public static AclClaim GetSessionClaim(ObjectId SessionId)
		{
			return new AclClaim(HordeClaimTypes.AgentSessionId, SessionId.ToString());
		}

		/// <summary>
		/// Determines whether the given user can masquerade as a given user
		/// </summary>
		/// <param name="User"></param>
		/// <param name="UserId"></param>
		/// <returns></returns>
		public Task<bool> AuthorizeAsUserAsync(ClaimsPrincipal User, UserId UserId)
		{
			UserId? CurrentUserId = User.GetUserId();
			if (CurrentUserId != null && CurrentUserId.Value == UserId)
			{
				return Task.FromResult(true);
			}
			else
			{
				return AuthorizeAsync(AclAction.Impersonate, User);
			}
		}
	}

	static class ClaimExtensions
	{
		public static bool HasSessionClaim(this ClaimsPrincipal User, ObjectId SessionId)
		{
			return User.HasClaim(HordeClaimTypes.AgentSessionId, SessionId.ToString());
		}

		public static ObjectId? GetSessionClaim(this ClaimsPrincipal User)
		{
			Claim? Claim = User.FindFirst(HordeClaimTypes.AgentSessionId);
			if (Claim == null || !ObjectId.TryParse(Claim.Value, out ObjectId SessionId))
			{
				return null;
			}
			else
			{
				return SessionId;
			}
		}
		
		public static string GetSessionClaimsAsString(this ClaimsPrincipal User)
		{
			return String.Join(",", User.Claims
				.Where(c => c.Type == HordeClaimTypes.AgentSessionId)
				.Select(c => c.Value));
		}
	}
}
