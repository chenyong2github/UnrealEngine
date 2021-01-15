// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Contains information about a stream, as returned by the 'p4 streams' command
	/// </summary>
	public class StreamsRecord
	{
		/// <summary>
		/// Path to the stream
		/// </summary>
		[PerforceTag("Stream")]
		public string Stream;

		/// <summary>
		/// Last time the stream definition was updated
		/// </summary>
		[PerforceTag("Update")]
		public DateTime Update;

		/// <summary>
		/// Last time the stream definition was accessed
		/// </summary>
		[PerforceTag("Access")]
		public DateTime Access;

		/// <summary>
		/// Owner of this stream
		/// </summary>
		[PerforceTag("Owner")]
		public string Owner;

		/// <summary>
		/// Name of the stream. This may be modified after the stream is initially created, but it's underlying depot path will not change.
		/// </summary>
		[PerforceTag("Name")]
		public string Name;

		/// <summary>
		/// The parent stream
		/// </summary>
		[PerforceTag("Parent", Optional = true)]
		public string? Parent;

		/// <summary>
		/// Type of the stream
		/// </summary>
		[PerforceTag("Type")]
		public StreamType Type;

		/// <summary>
		/// User supplied description of the stream
		/// </summary>
		[PerforceTag("desc", Optional = true)]
		public string? Description;

		/// <summary>
		/// Options for the stream definition
		/// </summary>
		[PerforceTag("Options")]
		public StreamsOptions Options;

		/// <summary>
		/// Whether this stream is more stable than the parent stream
		/// </summary>
		[PerforceTag("firmerThanParent")]
		public Nullable<bool> FirmerThanParent;

		/// <summary>
		/// Whether changes from this stream flow to the parent stream
		/// </summary>
		[PerforceTag("changeFlowsToParent")]
		public bool ChangeFlowsToParent;

		/// <summary>
		/// Whether changes from this stream flow from the parent stream
		/// </summary>
		[PerforceTag("changeFlowsFromParent")]
		public bool ChangeFlowsFromParent;

		/// <summary>
		/// The mainline branch associated with this stream
		/// </summary>
		[PerforceTag("baseParent", Optional = true)]
		public string? BaseParent;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private StreamsRecord()
		{
			Stream = null!;
			Owner = null!;
			Name = null!;
		}

		/// <summary>
		/// Summarize this record for display in the debugger
		/// </summary>
		/// <returns>Formatted stream information</returns>
		public override string? ToString()
		{
			return Name;
		}
	}
}
