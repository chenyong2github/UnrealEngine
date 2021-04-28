// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;
using System.Xml;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a Docker task
	/// </summary>
	public class DockerTaskParameters
	{
		/// <summary>
		/// Docker command line arguments
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments;

		/// <summary>
		/// Base directory for running the command
		/// </summary>
		[TaskParameter(Optional = true)]
		public string BaseDir;

		/// <summary>
		/// The minimum exit code, which is treated as an error.
		/// </summary>
		[TaskParameter(Optional = true)]
		public int ErrorLevel = 1;
	}

	/// <summary>
	/// Spawns Docker and waits for it to complete.
	/// </summary>
	[TaskElement("Docker", typeof(DockerTaskParameters))]
	public class DockerTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		DockerTaskParameters Parameters;

		/// <summary>
		/// Construct a Docker task
		/// </summary>
		/// <param name="InParameters">Parameters for the task</param>
		public DockerTask(DockerTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override void Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			FileReference ToolFile = CommandUtils.FindToolInPath("docker");
			if(ToolFile == null)
			{
				throw new AutomationException("Unable to find path to Docker. Check you have it installed, and it is on your PATH.");
			}

			IProcessResult Result = CommandUtils.Run(ToolFile.FullName, Parameters.Arguments, WorkingDir: Parameters.BaseDir);
			if (Result.ExitCode < 0 || Result.ExitCode >= Parameters.ErrorLevel)
			{
				throw new AutomationException("Docker terminated with an exit code indicating an error ({0})", Result.ExitCode);
			}
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			yield break;
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			yield break;
		}
	}
}
