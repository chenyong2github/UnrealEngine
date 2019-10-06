// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	/// A diagnostic message produced during the build
	/// </summary>
	[DataContract]
	[DebuggerDisplay("{Type}: {Message}")]
	public class InputDiagnostic
	{
		[DataMember(IsRequired = true)]
		public string Type;

		[DataMember(IsRequired = true)]
		public string Message;

		[DataMember(IsRequired = true)]
		public string Url;
	}

	/// <summary>
	/// Represents the outcome of a build step
	/// </summary>
	[DataContract]
	[DebuggerDisplay("{Name}")]
	public class InputJobStep
	{
		/// <summary>
		/// Name of the build step. Multiple step results can be submitted for a single build, and can be used to augment a single issue with additional diagnostic information.
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Name;

		/// <summary>
		/// Url of this job step
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Url;

		/// <summary>
		/// Base directory that the build was executed in. Optional. This is used to determine branch-relative file paths for diagnostics.
		/// </summary>
		[DataMember]
		public string BaseDirectory;

		/// <summary>
		/// Output from the build, typically messages which are already designated warnings or errors.
		/// </summary>
		[DataMember]
		public List<InputDiagnostic> Diagnostics = new List<InputDiagnostic>();
	}

	/// <summary>
	/// Class containing input data for a job
	/// </summary>
	[DataContract]
	[DebuggerDisplay("{Stream}: {Change} - {Name}")]
	class InputJob
	{
		/// <summary>
		/// Project that this build belongs to
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Project;

		/// <summary>
		/// Name of this build. Displayed in the UI.
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Name;

		/// <summary>
		/// Url for this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Url;

		/// <summary>
		/// Stream that was built
		/// </summary>
		[DataMember(IsRequired = true)]
		public string Stream;

		/// <summary>
		/// Changelist number that was built
		/// </summary>
		[DataMember(IsRequired = true)]
		public int Change;

		/// <summary>
		/// Steps that are part of this job
		/// </summary>
		[DataMember(IsRequired = true)]
		public List<InputJobStep> Steps = new List<InputJobStep>();
	}

	/// <summary>
	/// Main class containing input data for the build
	/// </summary>
	[DataContract]
	class InputData
	{
		/// <summary>
		/// Information about builds that have completed.
		/// </summary>
		[DataMember]
		public List<InputJob> Jobs = new List<InputJob>();
	}
}
