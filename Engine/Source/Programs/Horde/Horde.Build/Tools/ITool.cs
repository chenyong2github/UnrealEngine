// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Storage;
using Horde.Build.Acls;
using Horde.Build.Utilities;
using System;
using System.Collections.Generic;
using System.Security.Claims;
using System.Threading.Tasks;

namespace Horde.Build.Tools
{
	using ToolId = StringId<ITool>;
	using ToolDeploymentId = ObjectId<IToolDeployment>;

	/// <summary>
	/// Describes a standalone, external tool hosted and deployed by Horde. Provides basic functionality for performing
	/// gradual roll-out, versioning, etc...
	/// </summary>
	public interface ITool
	{
		/// <summary>
		/// Identifier for the tool
		/// </summary>
		ToolId Id { get; }

		/// <summary>
		/// Name of the tool
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Description for the tool
		/// </summary>
		string Description { get; }

		/// <summary>
		/// Last time that the tool was updated
		/// </summary>
		DateTime LastUpdateTime { get; }

		/// <summary>
		/// Current deployments of this tool, sorted by time.
		/// </summary>
		IReadOnlyList<IToolDeployment> Deployments { get; }

		/// <summary>
		/// Whether this tool should be exposed for download on a public endpoint without authentication
		/// </summary>
		bool Public { get; }

		/// <summary>
		/// Access list for this tool
		/// </summary>
		public AclV2? Acl { get; }
	}

	/// <summary>
	/// Current state of a tool's deployment
	/// </summary>
	public enum ToolDeploymentState
	{
		/// <summary>
		/// The deployment is ongoing
		/// </summary>
		Active,

		/// <summary>
		/// The deployment should be paused at its current state
		/// </summary>
		Paused,

		/// <summary>
		/// Deployment of this version is complete
		/// </summary>
		Complete,

		/// <summary>
		/// The deployment has been cancelled.
		/// </summary>
		Cancelled,
	}

	/// <summary>
	/// Deployment of a tool
	/// </summary>
	public interface IToolDeployment
	{
		/// <summary>
		/// Identifier for this deployment. A new identifier will be assigned to each created instance, so an identifier corresponds to a unique deployment.
		/// </summary>
		ToolDeploymentId Id { get; }

		/// <summary>
		/// Descriptive version string for this tool revision
		/// </summary>
		string Version { get; }

		/// <summary>
		/// Current state of this deployment
		/// </summary>
		ToolDeploymentState State { get; }

		/// <summary>
		/// Current progress of the deployment
		/// </summary>
		double Progress { get; }

		/// <summary>
		/// Last time at which the progress started. Set to null if the deployment was paused.
		/// </summary>
		DateTime? StartedAt { get; }

		/// <summary>
		/// Length of time over which to make the deployment
		/// </summary>
		TimeSpan Duration { get; }

		/// <summary>
		/// Mime-type for the data
		/// </summary>
		string MimeType { get; }

		/// <summary>
		/// Reference to this tool in Horde Storage.
		/// </summary>
		RefId RefId { get; }
	}

	/// <summary>
	/// Extension methods for tools
	/// </summary>
	public static class ToolExtensions
	{
		/// <summary>
		/// Test whether a user can perform an action on this tool 
		/// </summary>
		public static Task<bool> AuthorizeAsync(this ITool tool, AclAction action, ClaimsPrincipal user, AclService aclService, GlobalPermissionsCache? cache)
		{
			bool? result = tool.Acl?.Authorize(action, user);
			if (result == null)
			{
				return aclService.AuthorizeAsync(action, user, cache);
			}
			else
			{
				return Task.FromResult(result.Value);
			}
		}

		/// <summary>
		/// Get the progress fraction for a particular deployment and time
		/// </summary>
		/// <param name="deployment"></param>
		/// <param name="utcNow"></param>
		/// <returns></returns>
		public static double GetProgressValue(this IToolDeployment deployment, DateTime utcNow)
		{
			if (deployment.StartedAt == null)
			{
				return deployment.Progress;
			}
			else if (deployment.Duration > TimeSpan.Zero)
			{
				return Math.Clamp((utcNow - deployment.StartedAt.Value) / deployment.Duration, 0.0, 1.0);
			}
			else
			{
				return 1.0;
			}
		}
	}
}
