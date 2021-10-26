// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;

namespace HordeServer.Api
{
	/// <summary>
	/// Request sent by the Helix Swarm project
	/// The format of this request can be modified through the settings web UI in Swarm.
	/// Please ensure it matches the request class below.
	/// </summary>
	[SuppressMessage("Compiler", "CA1056:URI properties should not be strings")]
	public class SwarmAutoTestRequest
	{
		/// <summary>
		/// Name of the test
		/// </summary>
		public string? Test { get; set; } = null;
		
		/// <summary>
		/// ID of the test run
		/// </summary>
		public string? TestRunId { get; set; } = null;
		
		/// <summary>
		/// Perforce changelist number to run the test on
		/// </summary>
		[Required]
		public string? Changelist { get; set; } = null;
		
		/// <summary>
		/// Status of the changelist, for example "submitted"
		/// </summary>
		public string? Status { get; set; } = null;
		
		/// <summary>
		/// Version of the review
		/// </summary>
		public string? Version { get; set; } = null;
		
		/// <summary>
		/// Description of the review, usually the changelist message itself
		/// </summary>
		public string? Description { get; set; } = null;
		
		/// <summary>
		/// Swarm project ID
		/// </summary>
		public string? Project { get; set; } = null;
		
		/// <summary>
		/// Swarm project name
		/// </summary>
		public string? ProjectName { get; set; } = null;
		
		/// <summary>
		/// Perforce branch ID in Swarm
		/// </summary>
		public string? Branch { get; set; } = null;
		
		/// <summary>
		/// Perforce branch name in Swarm
		/// </summary>
		public string? BranchName { get; set; } = null;
		
		/// <summary>
		/// Callback URL to Swarm server to be called when "auto test" is finished.
		/// In our case, that would be when the job is completed.
		/// </summary>
		[Required]
		public string? UpdateUrl { get; set; } = null;
	}

	/// <summary>
	/// Response object describing the created job
	/// </summary>
	public class SwarmAutoTestResponse
	{
		/// <summary>
		/// The ID of the job that got started
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Id of the new document</param>
		public SwarmAutoTestResponse(string Id)
		{
			this.Id = Id;
		}
	}
}
