// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Command to be executed on a remote agent
	/// </summary>
	public class Command
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
		[CbField("d")]
		public Utf8String WorkingDirectory { get; set; } = Utf8String.Empty;

		/// <summary>
		/// List of output paths to be preserved on completion of the action. These may be files or directories.
		/// </summary>
		[CbField("p")]
		public List<Utf8String> OutputPaths { get; set; } = new List<Utf8String>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private Command()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Executable">The executable to run</param>
		/// <param name="Arguments">List of command line arguments.</param>
		public Command(Utf8String Executable, List<Utf8String> Arguments)
		{
			this.Executable = Executable;
			this.Arguments = Arguments;
		}
	}
}
