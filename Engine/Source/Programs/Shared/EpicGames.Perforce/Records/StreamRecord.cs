// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using EpicGames.Perforce;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Represents a Perforce stream spec
	/// </summary>
	public class StreamRecord
	{
		/// <summary>
		/// The stream path
		/// </summary>
		[PerforceTag("Stream")]
		public string Stream;

		/// <summary>
		/// The stream name
		/// </summary>
		[PerforceTag("Name")]
		public string Name;

		/// <summary>
		/// The Perforce user name of the user who owns the stream.
		/// </summary>
		[PerforceTag("Owner")]
		public string Owner;

		/// <summary>
		/// The date the stream specification was last modified.
		/// </summary>
		[PerforceTag("Update", Optional = true)]
		public DateTime Update;

		/// <summary>
		/// The date and time that the stream was last used in any way.
		/// </summary>
		[PerforceTag("Access", Optional = true)]
		public DateTime Access;

		/// <summary>
		/// The parent stream
		/// </summary>
		[PerforceTag("Parent", Optional = true)]
		public string? Parent;

		/// <summary>
		/// The stream type
		/// </summary>
		[PerforceTag("Type", Optional = true)]
		public string? Type;

		/// <summary>
		/// A textual description of the stream.
		/// </summary>
		[PerforceTag("Description", Optional = true)]
		public string? Description;

		/// <summary>
		/// Options for this stream
		/// </summary>
		[PerforceTag("Options")]
		public StreamOptions Options;

		/// <summary>
		/// List of paths in the stream spec
		/// </summary>
		[PerforceTag("Paths")]
		public List<string> Paths = new List<string>();

		/// <summary>
		/// Computed view for the stream
		/// </summary>
		[PerforceTag("View", Optional = true)]
		public List<string> View = new List<string>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private StreamRecord()
		{
			Stream = null!;
			Name = null!;
			Owner = null!;
		}
	}
}
