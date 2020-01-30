// Copyright Epic Games, Inc. All Rights Reserved.

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
	[DebuggerDisplay("{Change}: {JobName}")]
	class BuildHealthJobStep : IComparable<BuildHealthJobStep>
	{
		/// <summary>
		/// The id of this build on the server
		/// </summary>
		[DataMember(IsRequired = true)]
		public long Id = -1;

		/// <summary>
		/// The changelist that this build was run at.
		/// </summary>
		[DataMember(IsRequired = true)]
		public int Change;

		/// <summary>
		/// Name of this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public string JobName;

		/// <summary>
		/// Url for this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public string JobUrl;

		/// <summary>
		/// Name of this job step (typically null for sentinel builds)
		/// </summary>
		[DataMember(IsRequired = true)]
		public string JobStepName;

		/// <summary>
		/// Url for this job step (typically null for sentinel builds)
		/// </summary>
		[DataMember(IsRequired = true)]
		public string JobStepUrl;

		/// <summary>
		/// Url of the first error within this job
		/// </summary>
		[DataMember]
		public string ErrorUrl;

		/// <summary>
		/// Whether this build has been posted to the server or not
		/// </summary>
		[DataMember]
		public bool bPostedToServer = false;

		/// <summary>
		/// Constructor
		/// </summary>
		public BuildHealthJobStep(int Change, string JobName, string JobUrl, string JobStepName, string JobStepUrl, string ErrorUrl)
		{
			this.Change = Change;
			this.JobName = JobName;
			this.JobUrl = JobUrl;
			this.JobStepName = JobStepName;
			this.JobStepUrl = JobStepUrl;
			this.ErrorUrl = ErrorUrl;
		}

		/// <summary>
		/// Compares this build to another build
		/// </summary>
		/// <param name="Other">Build to compare to</param>
		/// <returns>Value indicating how the two builds should be ordered</returns>
		public int CompareTo(BuildHealthJobStep Other)
		{
			int Delta = Change - Other.Change;
			if (Delta == 0)
			{
				Delta = JobUrl.CompareTo(Other.JobUrl);
				if(Delta == 0)
				{
					Delta = JobStepUrl.CompareTo(Other.JobStepUrl);
				}
			}
			return Delta;
		}
	}
}
