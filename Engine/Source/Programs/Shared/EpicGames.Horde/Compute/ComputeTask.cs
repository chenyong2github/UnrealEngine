// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Describes an action to be executed in a particular workspace
	/// </summary>
	public class ComputeTask
	{
		/// <summary>
		/// The executable to run
		/// </summary>
		[CbField("e")]
		public Utf8String Executable { get; set; }

		/// <summary>
		/// List of command line arguments for the process to run.
		/// </summary>
		[CbField("a")]
		public List<Utf8String> Arguments { get; set; } = new List<Utf8String>();

		/// <summary>
		/// Environment variables to set for the child process
		/// </summary>
		[CbField("v")]
		public Dictionary<Utf8String, Utf8String> EnvVars { get; set; } = new Dictionary<Utf8String, Utf8String>();

		/// <summary>
		/// Path to the working directory within the workspace
		/// </summary>
		[CbField("w")]
		public Utf8String WorkingDirectory { get; set; }

		/// <summary>
		/// Hash of a <see cref="DirectoryTree"/> object encoded as a CbObject and stored in the CAS
		/// </summary>
		[CbField("s")]
		public CbObjectAttachment SandboxHash { get; set; }

		/// <summary>
		/// Requirements for the agent to execute the work
		/// </summary>
		[CbField("r")]
		public CbObjectAttachment RequirementsHash { get; set; } = CbObjectAttachment.Zero;

		/// <summary>
		/// List of output paths to be captured on completion of the action. These may be files or directories.
		/// </summary>
		[CbField("o")]
		public List<Utf8String> OutputPaths { get; set; } = new List<Utf8String>();

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		private ComputeTask()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Executable">The executable to run</param>
		/// <param name="Arguments">Arguments for the executable to run</param>
		/// <param name="WorkingDirectory">Working directory for execution</param>
		/// <param name="SandboxHash">Hash of the sandbox</param>
		public ComputeTask(Utf8String Executable, List<Utf8String> Arguments, Utf8String WorkingDirectory, CbObjectAttachment SandboxHash)
		{
			this.Executable = Executable;
			this.Arguments = Arguments;
			this.WorkingDirectory = WorkingDirectory;
			this.SandboxHash = SandboxHash;
		}
	}
}
