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
		/// Creates a summary for this issue
		/// </summary>
		/// <returns>Summary for this issue</returns>
		public string GetSummary()
		{
			StringBuilder Result = new StringBuilder();

			SortedSet<string> StreamNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach(string StreamName in Streams.Keys)
			{
				string TrimStreamName = StreamName.TrimEnd('/');

				int Idx = TrimStreamName.LastIndexOf('/');
				if(Idx != -1)
				{
					TrimStreamName = TrimStreamName.Substring(Idx + 1);
				}

				StreamNames.Add(TrimStreamName);
			}
			Result.AppendFormat("[{0}] ", String.Join("/", StreamNames));

			if (Fingerprint.Category == TrackedIssueFingerprintCategory.Code)
			{
				SortedSet<string> ShortFileNames = GetSourceFileNames();
				if (ShortFileNames.Count == 0)
				{
					Result.Append("Compile errors");
				}
				else
				{
					Result.AppendFormat("Compile errors in {0}", String.Join(", ", ShortFileNames));
				}
			}
			else if (Fingerprint.Category == TrackedIssueFingerprintCategory.Content)
			{
				SortedSet<string> ShortFileNames = GetAssetNames();
				if (ShortFileNames.Count == 0)
				{
					Result.Append("Content errors");
				}
				else
				{
					Result.AppendFormat("Content errors in {0}", String.Join(", ", ShortFileNames));
				}
			}
			else
			{
				SortedSet<string> StepNames = GetStepNames();
				Result.AppendFormat("Errors in {0}", String.Join(", ", StepNames));
			}

			const int MaxLength = 128;
			if (Result.Length > MaxLength)
			{
				Result.Remove(MaxLength, Result.Length - MaxLength);
				Result.Append("...");
			}

			return Result.ToString();

			throw new NotImplementedException();
		}

		/// <summary>
		/// Gets a set of unique source file names that relate to this issue
		/// </summary>
		/// <returns>Set of source file names</returns>
		private SortedSet<string> GetSourceFileNames()
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in Fingerprint.FileNames)
			{
				int Idx = FileName.LastIndexOfAny(new char[] { '/', '\\' });
				if (Idx != -1)
				{
					string ShortFileName = FileName.Substring(Idx + 1);
					if (!ShortFileName.StartsWith("Module.", StringComparison.OrdinalIgnoreCase))
					{
						ShortFileNames.Add(ShortFileName);
					}
				}
			}
			return ShortFileNames;
		}

		/// <summary>
		/// Gets a set of unique asset filenames that relate to this issue
		/// </summary>
		/// <returns>Set of asset names</returns>
		private SortedSet<string> GetAssetNames()
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in Fingerprint.FileNames)
			{
				int Idx = FileName.LastIndexOfAny(new char[] { '/', '\\' });
				if (Idx != -1)
				{
					string AssetName = FileName.Substring(Idx + 1);

					int DotIdx = AssetName.LastIndexOf('.');
					if (DotIdx != -1)
					{
						AssetName = AssetName.Substring(0, DotIdx);
					}

					ShortFileNames.Add(AssetName);
				}
			}
			return ShortFileNames;
		}

		/// <summary>
		/// Finds all the steps which are related to this issue
		/// </summary>
		/// <returns>Set of step names</returns>
		private SortedSet<string> GetStepNames()
		{
			return new SortedSet<string>(Streams.Values.SelectMany(x => x.FailedBuilds.SelectMany(y => y.StepNames)));
		}

		/// <summary>
		/// Format the issue for the debugger
		/// </summary>
		/// <returns>String representation of the issue</returns>
		public override string ToString()
		{
			string Summary = GetSummary();
			if(Id == -1)
			{
				return String.Format("[New] {0}", Summary);
			}
			else
			{
				return String.Format("[{0}] {1}", Id, Summary);
			}
		}
	}
}
