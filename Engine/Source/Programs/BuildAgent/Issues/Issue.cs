// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Issues
{
	/// <summary>
	/// Information about a particular issue
	/// </summary>
	[DataContract]
	class Issue
	{
		/// <summary>
		/// The issue id in the database. -1 for issues that have not been posted yet.
		/// </summary>
		[DataMember(Order = 0, IsRequired = true)]
		public long Id = -1;

		/// <summary>
		/// Project that this belongs to
		/// </summary>
		[DataMember(Order = 1, IsRequired = true)]
		public string Project;

		/// <summary>
		/// Type common to all diagnostics within this issue.
		/// </summary>
		[DataMember(Order = 2, IsRequired = true)]
		public string Category;

		/// <summary>
		/// The initial job that this error was seen on. Allows other issues from the same job to be merged.
		/// </summary>
		[DataMember(Order = 3, IsRequired = true)]
		public string InitialJobUrl;

		/// <summary>
		/// List of strings to display in the details panel for this job
		/// </summary>
		[DataMember(Order = 4, IsRequired = true)]
		public List<IssueDiagnostic> Diagnostics = new List<IssueDiagnostic>();

		/// <summary>
		/// List of files associated with this issue
		/// </summary>
		[DataMember(Order = 5)]
		public HashSet<string> FileNames = new HashSet<string>();

		/// <summary>
		/// List of messages associated with this issue
		/// </summary>
		[DataMember(Order = 6)]
		public SortedSet<string> Identifiers = new SortedSet<string>();

		/// <summary>
		/// Other arbitrary references to assets or files
		/// </summary>
		[DataMember(Order = 7)]
		public SortedSet<string> References = new SortedSet<string>();

		/// <summary>
		/// The last posted issue summary. Will be updated if it changes.
		/// </summary>
		[DataMember(Order = 20)]
		public string PostedSummary;

		/// <summary>
		/// Whether we've posted an update to the resolved flag to the server
		/// </summary>
		[DataMember(Order = 22)]
		public bool bPostedResolved;

		/// <summary>
		/// The time at which the issue was closed. Issues will be retained for 24 hours after they are closed, in case the same issue appears in another stream and to prevent the issue being added again. 
		/// </summary>
		[DataMember(Order = 23)]
		public DateTime? ResolvedAt;

		/// <summary>
		/// Map of stream name -> step name -> history for builds exhibiting this issue
		/// </summary>
		[DataMember(Order = 30, IsRequired = true)]
		public Dictionary<string, Dictionary<string, IssueHistory>> Streams = new Dictionary<string, Dictionary<string, IssueHistory>>();

		/// <summary>
		/// Set of changes which may have caused this issue. Used to de-duplicate issues between streams.
		/// </summary>
		[DataMember(Order = 31)]
		public HashSet<int> SourceChanges = new HashSet<int>();

		/// <summary>
		/// List of possible causers 
		/// </summary>
		[DataMember(Order = 32)]
		public HashSet<string> Watchers = new HashSet<string>();

		/// <summary>
		/// Set of causers that have yet to be added to the possible causers list
		/// </summary>
		[DataMember(Order = 33)]
		public HashSet<string> PendingWatchers = new HashSet<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Category">Category of this issue</param>
		/// <param name="InitialJobUrl">Url of the initial job that this error was seen with</param>
		/// <param name="ErrorUrl"></param>
		public Issue(string Project, string Category, string InitialJobUrl, IssueDiagnostic Diagnostic)
		{
			this.Project = Project;
			this.Category = Category;
			this.InitialJobUrl = InitialJobUrl;
			this.Diagnostics.Add(Diagnostic);
		}

		/// <summary>
		/// Determines whether the issue can be closed
		/// </summary>
		/// <returns>True if the issue can be closed</returns>
		public bool IsResolved()
		{
			return Streams.Values.All(x => x.Values.All(y => y.NextSuccessfulBuild != null));
		}

		/// <summary>
		/// Finds all the steps which are related to this issue
		/// </summary>
		/// <returns>Set of step names</returns>
		public SortedSet<string> GetStepNames()
		{
			return new SortedSet<string>(Streams.Values.SelectMany(x => x.Keys));
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
