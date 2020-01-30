// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Issues
{
	/// <summary>
	/// History of builds within a particular stream that contribute to an issue
	/// </summary>
	[DataContract]
	class IssueHistory
	{
		/// <summary>
		/// The previous build before it started failing. This should be updated as new builds come in.
		/// </summary>
		[DataMember]
		public IssueBuild PrevSuccessfulBuild;

		/// <summary>
		/// List of failing builds contributing to this issue
		/// </summary>
		[DataMember]
		public List<IssueBuild> FailedBuilds = new List<IssueBuild>();

		/// <summary>
		/// The first successful build after the failures.
		/// </summary>
		[DataMember]
		public IssueBuild NextSuccessfulBuild;

		/// <summary>
		/// Constructs a new history for a particular stream
		/// </summary>
		public IssueHistory(IssueBuild PrevSuccessfulBuild)
		{
			this.PrevSuccessfulBuild  = PrevSuccessfulBuild;
		}

		/// <summary>
		/// Adds a failed build to this object
		/// </summary>
		/// <param name="Build">The failed build</param>
		public void AddFailedBuild(IssueBuild Build)
		{
			int Index = FailedBuilds.BinarySearch(Build);
			if (Index < 0)
			{
				FailedBuilds.Insert(~Index, Build);
			}
		}

		/// <summary>
		/// Determines whether a given build can be added to an issue. This filters cases where an issue does not already have builds for the given stream, or where there is a successful build between the new build and known failures for this issue.
		/// </summary>
		/// <param name="Build">The build to add</param>
		public bool CanAddFailedBuild(int Change)
		{
			// Check that this build is not after a succesful build
			if(NextSuccessfulBuild != null && Change >= NextSuccessfulBuild.Change)
			{
				return false;
			}

			// Check that this build is not before the last known successful build
			if(PrevSuccessfulBuild != null && Change <= PrevSuccessfulBuild.Change)
			{
				return false;
			}

			// Otherwise allow it
			return true;
		}

		/// <summary>
		/// Enumerates all the builds in this stream
		/// </summary>
		public IEnumerable<IssueBuild> Builds
		{
			get
			{
				if(PrevSuccessfulBuild != null)
				{
					yield return PrevSuccessfulBuild;
				}
				foreach(IssueBuild FailedBuild in FailedBuilds)
				{
					yield return FailedBuild;
				}
				if(NextSuccessfulBuild != null)
				{
					yield return NextSuccessfulBuild;
				}
			}
		}
	}
}
