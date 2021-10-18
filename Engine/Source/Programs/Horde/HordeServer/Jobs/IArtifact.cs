// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

namespace HordeServer.Models
{
	/// <summary>
	/// Information about an artifact
	/// </summary>
	public class Artifact
	{
		/// <summary>
		/// Identifier for the Artifact. Randomly generated.
		/// </summary>
		[BsonRequired, BsonId]
		public ObjectId Id { get; set; }

		/// <summary>
		/// Unique id of the job containing this artifact
		/// </summary>
		[BsonRequired]
		public ObjectId JobId { get; set; }

		/// <summary>
		/// Unique id of the job containing this artifact
		/// </summary>
		[BsonRequired]
		public string Name { get; set; }

		/// <summary>
		/// Unique id of the step containing this artifact
		/// </summary>
		public SubResourceId? StepId { get; set; }

		/// <summary>
		/// Total size of the file
		/// </summary>
		[BsonRequired]
		public long Length { get; set; }

		/// <summary>
		/// Type of artifact
		/// </summary>
		public string MimeType { get; set; }

		/// <summary>
		/// Version of the file in the db
		/// </summary>
		[BsonRequired]
		public int UpdateIndex { get; set; }

		/// <summary>
		/// Private constructor for BSON serializer
		/// </summary>
		[BsonConstructor]
		private Artifact()
		{
			this.Name = null!;
			this.MimeType = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobId">Unique id for the job containing this artifact</param>
		/// <param name="StepId">Optional unique id for the step containing this artifact</param>
		/// <param name="Name">Name</param>
		/// <param name="Length">File Length</param>
		/// <param name="MimeType">Type of artifact</param>
		public Artifact(ObjectId JobId, SubResourceId? StepId, string Name, long Length, string MimeType)
		{
			this.Id = ObjectId.GenerateNewId();
			this.JobId = JobId;
			this.StepId = StepId;
			this.Name = Name;
			this.Length = Length;
			this.MimeType = MimeType;
		}
	}
}
