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
	/// Information about a particular issue
	/// </summary>
	[DataContract]
	class TrackedIssue
	{
		/// <summary>
		/// The issue id in the database. -1 for issues that have not been posted yet.
		/// </summary>
		[DataMember(IsRequired = true)]
		public long Id = -1;

		/// <summary>
		/// The last posted issue summary. Will be updated if it changes.
		/// </summary>
		[DataMember]
		public string PostedSummary;

		/// <summary>
		/// The last posted issue details. Will be updated if it changes.
		/// </summary>
		[DataMember]
		public string PostedDetails;

		/// <summary>
		/// Type common to all diagnostics within this issue.
		/// </summary>
		[DataMember(IsRequired = true)]
		public TrackedIssueFingerprint Fingerprint;

		/// <summary>
		/// The initial change that this issue was seen on. We will allow additional diagnostics from the same build to be appended.
		/// </summary>
		[DataMember]
		public int InitialChange;

		/// <summary>
		/// All the streams that are exhibiting this issue
		/// </summary>
		[DataMember(IsRequired = true)]
		public Dictionary<string, TrackedIssueHistory> Streams = new Dictionary<string, TrackedIssueHistory>();

		/// <summary>
		/// Set of changes which may have caused this issue. Used to de-duplicate issues between streams.
		/// </summary>
		[DataMember]
		public HashSet<int> SourceChanges = new HashSet<int>();

		/// <summary>
		/// List of possible causers 
		/// </summary>
		[DataMember]
		public HashSet<string> Watchers = new HashSet<string>();

		/// <summary>
		/// Set of causers that have yet to be added to the possible causers list
		/// </summary>
		[DataMember]
		public HashSet<string> PendingWatchers = new HashSet<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">Type name for this issue</param>
		/// <param name="InitialChange">Initial build changelist that this issue was seen on</param>
		public TrackedIssue(TrackedIssueFingerprint Fingerprint, int InitialChange)
		{
			this.Fingerprint = Fingerprint;
			this.InitialChange = InitialChange;
		}

		/// <summary>
		/// Determines whether the issue can be closed
		/// </summary>
		/// <returns>True if the issue can be closed</returns>
		public bool CanClose()
		{
			return Streams.Values.All(x => x.NextSuccessfulBuild != null);
		}

		/// <summary>
		/// Finds all the steps which are related to this issue
		/// </summary>
		/// <returns>Set of step names</returns>
		public SortedSet<string> GetStepNames()
		{
			return new SortedSet<string>(Streams.Values.SelectMany(x => x.FailedBuilds.SelectMany(y => y.StepNames)));
		}

		/// <summary>
		/// Format the issue for the debugger
		/// </summary>
		/// <returns>String representation of the issue</returns>
		public override string ToString()
		{
			StringBuilder Result = new StringBuilder();
			if(Id == -1)
			{
				Result.Append("[New] ");
			}
			else
			{
				Result.AppendFormat("[{0}] ", Id);
			}
			Result.AppendFormat("Errors in {0}", String.Join(", ", GetStepNames()));
			return Result.ToString();
		}
	}
}
