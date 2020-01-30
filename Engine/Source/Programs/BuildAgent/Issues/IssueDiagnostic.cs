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
	/// Information about a message
	/// </summary>
	[DataContract]
	[DebuggerDisplay("{Message}")]
	public class IssueDiagnostic
	{
		/// <summary>
		/// Name of this job step
		/// </summary>
		[DataMember(Order = 0, IsRequired = true)]
		public string JobStepName;

		/// <summary>
		/// The url of this job step
		/// </summary>
		[DataMember(Order = 1)]
		public string JobStepUrl;

		/// <summary>
		/// The diagnostic message
		/// </summary>
		[DataMember(Order = 2, IsRequired = true)]
		public string Message;

		/// <summary>
		/// Url to the error
		/// </summary>
		[DataMember(Order = 3, IsRequired = true)]
		public string ErrorUrl;

		/// <summary>
		/// Whether or not this diagnostic has been posted to the server
		/// </summary>
		[DataMember(Order = 4)]
		public bool bPostedToServer = false;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobStepName">Name of the job step</param>
		/// <param name="JobStepUrl">Url of the job step</param>
		/// <param name="Message">Message to display</param>
		/// <param name="ErrorUrl">Url to link to</param>
		public IssueDiagnostic(string JobStepName, string JobStepUrl, string Message, string ErrorUrl)
		{
			this.JobStepName = JobStepName;
			this.JobStepUrl = JobStepUrl;
			this.Message = Message;
			this.ErrorUrl = ErrorUrl;
		}
	}
}
