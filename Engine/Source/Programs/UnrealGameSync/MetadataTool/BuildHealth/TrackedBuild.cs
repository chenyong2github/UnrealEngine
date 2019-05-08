// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace MetadataTool
{
	/// <summary>
	/// Represents information about a build associated with a particular issue
	/// </summary>
	[DataContract]
	[DebuggerDisplay("{Change}: {Name}")]
	class TrackedBuild : IComparable<TrackedBuild>
	{
		/// <summary>
		/// Name of this build
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Name;

		/// <summary>
		/// The changelist that this build was run at.
		/// </summary>
		[DataMember(IsRequired = true)]
		public int Change;

		/// <summary>
		/// Unique id to identify this job (typically the job url)
		/// </summary>
		[DataMember(IsRequired = true)]
		public string UniqueId;

		/// <summary>
		/// Url of the first error within this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Url;

		/// <summary>
		/// Set of step names that contributed to this build.
		/// </summary>
		[DataMember(IsRequired = true)]
		public HashSet<string> StepNames = new HashSet<string>();

		/// <summary>
		/// Whether this build has been posted to the server or not
		/// </summary>
		[DataMember]
		public bool bPostedToServer = false;

		/// <summary>
		/// Constructor
		/// </summary>
		public TrackedBuild(string Name, int Change, string UniqueId, string Url)
		{
			this.Name = Name;
			this.Change = Change;
			this.UniqueId = UniqueId;
			this.Url = Url;
		}

		/// <summary>
		/// Compares this build to another build
		/// </summary>
		/// <param name="Other">Build to compare to</param>
		/// <returns>Value indicating how the two builds should be ordered</returns>
		public int CompareTo(TrackedBuild Other)
		{
			int Delta = Change - Other.Change;
			if (Delta == 0)
			{
				Delta = Name.CompareTo(Other.Name);
				if(Delta == 0)
				{
					Delta = UniqueId.CompareTo(Other.UniqueId);
				}
			}
			return Delta;
		}
	}
}
