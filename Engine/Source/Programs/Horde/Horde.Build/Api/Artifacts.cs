// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;

namespace HordeServer.Api
{
	/// <summary>
	/// Response from creating an artifact
	/// </summary>
	public class CreateArtifactResponse
	{
		/// <summary>
		/// Unique id for this artifact
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">The artifact file id</param>
		public CreateArtifactResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Request to get a zip of artifacts
	/// </summary>
	public class GetArtifactZipRequest
	{
		/// <summary>
		/// Job id to get all artifacts for
		/// </summary>
		public string? JobId { get; set; }

		/// <summary>
		/// Step id to filter by
		/// </summary>
		public string? StepId { get; set; }

		/// <summary>
		/// Further filter by a list of artifact ids
		/// </summary>
		public List<string>? ArtifactIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobId">Job id to get all artifacts for</param>
		/// <param name="StepId">step to filter by</param>
		/// <param name="ArtifactIds">The artifact ids.  Returns all artifacts for a job </param>
		public GetArtifactZipRequest(string? JobId, string? StepId, List<string>? ArtifactIds)
		{
			this.JobId = JobId;
			this.StepId = StepId;
			this.ArtifactIds = ArtifactIds;
		}
	}

	/// <summary>
	/// Response describing an artifact
	/// </summary>
	public class GetArtifactResponse
	{
		/// <summary>
		/// Unique id of the artifact
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the artifact
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Unique id of the job for this artifact
		/// </summary>
		public string JobId { get; set; }

		/// <summary>
		/// Optional unique id of the step for this artifact
		/// </summary>
		public string? StepId { get; set; }

		/// <summary>
		/// Type of artifact
		/// </summary>
		public string MimeType { get; set; }

		/// <summary>
		/// Length of artifact
		/// </summary>
		public long Length { get; set; }

		/// <summary>
		/// Short-lived code that can be used to download this artifact through direct download links in the browser
		/// </summary>
		public string? Code { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Artifact">The artifact to construct from</param>
		/// <param name="Code">The direct download code</param>
		public GetArtifactResponse(IArtifact Artifact, string? Code)
		{
			this.Id = Artifact.Id.ToString();
			this.Name = Artifact.Name;
			this.JobId = Artifact.JobId.ToString();
			this.StepId = Artifact.StepId.ToString();
			this.MimeType = Artifact.MimeType;
			this.Length = Artifact.Length;
			this.Code = Code;
		}
	}
}
