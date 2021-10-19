// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

namespace HordeServer.Models
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Information about an artifact
	/// </summary>
	public interface IArtifact
	{
		/// <summary>
		/// Identifier for the Artifact. Randomly generated.
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// Unique id of the job containing this artifact
		/// </summary>
		public JobId JobId { get; }

		/// <summary>
		/// Unique id of the job containing this artifact
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Unique id of the step containing this artifact
		/// </summary>
		public SubResourceId? StepId { get; }

		/// <summary>
		/// Total size of the file
		/// </summary>
		public long Length { get; }

		/// <summary>
		/// Type of artifact
		/// </summary>
		public string MimeType { get; }
	}
}
