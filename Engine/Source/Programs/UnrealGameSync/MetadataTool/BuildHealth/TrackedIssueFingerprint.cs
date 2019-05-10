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
	/// Used to group diagnostics together
	/// </summary>
	[DataContract]
	class TrackedIssueFingerprint
	{
		/// <summary>
		/// Type common to all diagnostics within this issue.
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Category;

		/// <summary>
		/// The initial change that this changelist was seen on. Allows other fingerprints at the same change to be merged.
		/// </summary>
		[DataMember(IsRequired = true)]
		public int InitialChange;

		/// <summary>
		/// Summary for this issue
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Summary;

		/// <summary>
		/// List of strings to display in the details panel for this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public List<string> Details = new List<string>();

		/// <summary>
		/// Url for this issue
		/// </summary>
		[DataMember(IsRequired = true)]
		public string ErrorUrl;

		/// <summary>
		/// List of files associated with this issue
		/// </summary>
		[DataMember]
		public HashSet<string> FileNames = new HashSet<string>();

		/// <summary>
		/// List of messages associated with this issue
		/// </summary>
		[DataMember]
		public SortedSet<string> Messages = new SortedSet<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Category">Category for this issue</param>
		/// <param name="Summary">The summary text for this issue</param>
		/// <param name="ErrorUrl">The Url for this issue</param>
		/// <param name="InitialChange">The initial change that this issue was seen on. Fingerprints in the same category at the same change can be merged as long as the type matches.</param>
		public TrackedIssueFingerprint(string Category, string Summary, string ErrorUrl, int InitialChange)
		{
			this.Category = Category;
			this.Summary = Summary;
			this.ErrorUrl = ErrorUrl;
			this.InitialChange = InitialChange;
		}

		/// <summary>
		/// Finds all the unique filenames without their path components
		/// </summary>
		/// <returns>Set of sorted filenames</returns>
		public static SortedSet<string> GetFileNamesWithoutPath(IEnumerable<string> FileNames)
		{
			SortedSet<string> FileNamesWithoutPath = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
			{
				int Idx = FileName.LastIndexOf('/');
				if (Idx != -1)
				{
					FileNamesWithoutPath.Add(FileName.Substring(Idx + 1));
				}
			}
			return FileNamesWithoutPath;
		}

		/// <summary>
		/// Gets a set of unique source file names that relate to this issue
		/// </summary>
		/// <returns>Set of source file names</returns>
		public static SortedSet<string> GetSourceFileNames(IEnumerable<string> FileNames)
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
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
		public static SortedSet<string> GetAssetNames(IEnumerable<string> FileNames)
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
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
		/// Formats this object for debugging
		/// </summary>
		/// <returns>String representation of the issue</returns>
		public override string ToString()
		{
			return Summary;
		}
	}

}
