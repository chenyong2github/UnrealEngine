// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServer.Jobs
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Interface for a collection of artifacts
	/// </summary>
	public interface IArtifactCollection
	{
		/// <summary>
		/// Creates a new artifact
		/// </summary>
		/// <param name="JobId">Unique id of the job that owns this artifact</param>
		/// <param name="StepId">Optional Step id</param>
		/// <param name="Name">Name of artifact</param>
		/// <param name="MimeType">Type of artifact</param>
		/// <param name="Data">The data to write</param>
		/// <returns>The new log file document</returns>
		Task<IArtifact> CreateArtifactAsync(JobId JobId, SubResourceId? StepId, string Name, string MimeType, System.IO.Stream Data);

		/// <summary>
		/// Gets all the available artifacts for a job
		/// </summary>
		/// <param name="JobId">Unique id of the job to query</param>
		/// <param name="StepId">Unique id of the Step to query</param>
		/// <param name="Name">Name of the artifact</param>
		/// <returns>List of artifact documents</returns>
		Task<List<IArtifact>> GetArtifactsAsync(JobId? JobId, SubResourceId? StepId, string? Name);

		/// <summary>
		/// Gets a specific list of artifacts based on id
		/// </summary>
		/// <param name="ArtifactIds">The list of artifact Ids</param>
		/// <returns>List of artifact documents</returns>
		Task<List<IArtifact>> GetArtifactsAsync(IEnumerable<ObjectId> ArtifactIds);

		/// <summary>
		/// Gets an artifact by ID
		/// </summary>
		/// <param name="ArtifactId">Unique id of the artifact</param>
		/// <returns>The artifact document</returns>
		Task<IArtifact?> GetArtifactAsync(ObjectId ArtifactId);

		/// <summary>
		/// Updates an artifact
		/// </summary>
		/// <param name="Artifact">The artifact</param>
		/// <param name="NewMimeType">New mime type</param>
		/// <param name="NewData">New data</param>
		/// <returns>Async task</returns>
		Task<bool> UpdateArtifactAsync(IArtifact? Artifact, string NewMimeType, System.IO.Stream NewData);

		/// <summary>
		/// gets artifact data
		/// </summary>
		/// <param name="Artifact">The artifact</param>
		/// <returns>The chunk data</returns>
		Task<System.IO.Stream> OpenArtifactReadStreamAsync(IArtifact Artifact);
	}
}
