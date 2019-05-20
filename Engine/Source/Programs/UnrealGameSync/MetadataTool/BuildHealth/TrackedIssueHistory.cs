// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace MetadataTool
{
	/// <summary>
	/// History of builds within a particular stream that contribute to an issue
	/// </summary>
	[DataContract]
	class BuildHealthJobHistory
	{
		/// <summary>
		/// The previous build before it started failing. This should be updated as new builds come in.
		/// </summary>
		[DataMember]
		public BuildHealthJobStep PrevSuccessfulBuild;

		/// <summary>
		/// List of failing builds contributing to this issue
		/// </summary>
		[DataMember]
		public List<BuildHealthJobStep> FailedBuilds = new List<BuildHealthJobStep>();

		/// <summary>
		/// The first successful build after the failures.
		/// </summary>
		[DataMember]
		public BuildHealthJobStep NextSuccessfulBuild;

		/// <summary>
		/// Constructs a new history for a particular stream
		/// </summary>
		public BuildHealthJobHistory(BuildHealthJobStep PrevSuccessfulBuild)
		{
			this.PrevSuccessfulBuild  = PrevSuccessfulBuild;
		}

		/// <summary>
		/// Adds a failed build to this object
		/// </summary>
		/// <param name="Build">The failed build</param>
		public void AddFailedBuild(BuildHealthJobStep Build)
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
		public IEnumerable<BuildHealthJobStep> Builds
		{
			get
			{
				if(PrevSuccessfulBuild != null)
				{
					yield return PrevSuccessfulBuild;
				}
				foreach(BuildHealthJobStep FailedBuild in FailedBuilds)
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
