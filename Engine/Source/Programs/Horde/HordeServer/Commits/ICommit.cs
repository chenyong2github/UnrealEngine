// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Commits
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Stores metadata about a commit
	/// </summary>
	interface ICommit
	{
		/// <summary>
		/// Unique id for this document
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// The stream id
		/// </summary>
		public StreamId StreamId { get; }

		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// The change that this commit originates from
		/// </summary>
		public int OriginalChange { get; }

		/// <summary>
		/// The author user id
		/// </summary>
		public ObjectId AuthorId { get; }

		/// <summary>
		/// The owner of this change, if different from the author (due to Robomerge)
		/// </summary>
		public ObjectId OwnerId { get; }

		/// <summary>
		/// Changelist description
		/// </summary>
		public string Description { get; }

		/// <summary>
		/// Base path for all files in the change
		/// </summary>
		public string BasePath { get; }

		/// <summary>
		/// Date/time that change was committed
		/// </summary>
		public DateTime DateUtc { get; }
	}

	/// <summary>
	/// New commit object
	/// </summary>
	class NewCommit
	{
		/// <inheritdoc cref="ICommit.StreamId"/>
		public StreamId StreamId { get; set; }

		/// <inheritdoc cref="ICommit.Change"/>
		public int Change { get; set; }

		/// <inheritdoc cref="ICommit.OriginalChange"/>
		public int OriginalChange { get; set; }

		/// <inheritdoc cref="ICommit.AuthorId"/>
		public ObjectId AuthorId { get; set; }

		/// <inheritdoc cref="ICommit.OwnerId"/>
		public ObjectId? OwnerId { get; set; }

		/// <inheritdoc cref="ICommit.Description"/>
		public string Description { get; set; }

		/// <inheritdoc cref="ICommit.BasePath"/>
		public string BasePath { get; set; }

		/// <inheritdoc cref="ICommit.DateUtc"/>
		public DateTime DateUtc { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public NewCommit(StreamId StreamId, int Change, int OriginalChange, ObjectId AuthorId, ObjectId OwnerId, string Description, string BasePath, DateTime DateUtc)
		{
			this.StreamId = StreamId;
			this.Change = Change;
			this.OriginalChange = OriginalChange;
			this.AuthorId = AuthorId;
			this.OwnerId = OwnerId;
			this.Description = Description;
			this.BasePath = BasePath;
			this.DateUtc = DateUtc;
		}
	}
}
