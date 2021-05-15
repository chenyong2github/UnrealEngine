// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Xml;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a Docker-Build task
	/// </summary>
	public class DockerPushTaskParameters
	{
		/// <summary>
		/// Repository
		/// </summary>
		[TaskParameter]
		public string Repository;

		/// <summary>
		/// Source image to push
		/// </summary>
		[TaskParameter]
		public string Image;

		/// <summary>
		/// Name of the target image
		/// </summary>
		[TaskParameter(Optional = true)]
		public string TargetImage;

		/// <summary>
		/// Settings for logging in to AWS ECR
		/// </summary>
		[TaskParameter(Optional = true)]
		public string AwsSettings;
	}

	/// <summary>
	/// Spawns Docker and waits for it to complete.
	/// </summary>
	[TaskElement("Docker-Push", typeof(DockerPushTaskParameters))]
	public class DockerPushTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		DockerPushTaskParameters Parameters;

		/// <summary>
		/// Construct a Docker task
		/// </summary>
		/// <param name="InParameters">Parameters for the task</param>
		public DockerPushTask(DockerPushTaskParameters InParameters)
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
			FileReference DockerExe = CommandUtils.FindToolInPath("docker");
			if(DockerExe == null)
			{
				throw new AutomationException("Unable to find path to Docker. Check you have it installed, and it is on your PATH.");
			}

			Log.TraceInformation("Pushing Docker image");
			using (LogIndentScope Scope = new LogIndentScope("  "))
			{
				if (Parameters.AwsSettings != null)
				{
					string Password = AwsTask.Run("ecr get-login-password", ResolveFile(Parameters.AwsSettings), LogOutput: false);
					RunDocker(DockerExe, $"login {Parameters.Repository} --username AWS --password-stdin", Password, CommandUtils.RootDirectory);
				}

				string TargetImage = Parameters.TargetImage ?? Parameters.Image;
				RunDocker(DockerExe, $"tag {Parameters.Image} {Parameters.Repository}/{TargetImage}", null, CommandUtils.RootDirectory);
				RunDocker(DockerExe, $"push {Parameters.Repository}/{TargetImage}", null, CommandUtils.RootDirectory);
			}
		}

		/// <summary>
		/// Runs Docker
		/// </summary>
		/// <param name="DockerExe"></param>
		/// <param name="Arguments"></param>
		/// <param name="StagingDir"></param>
		public void RunDocker(FileReference DockerExe, string Arguments, string Input, DirectoryReference StagingDir)
		{
			IProcessResult Result = CommandUtils.Run(DockerExe.FullName, Arguments, Input: Input, WorkingDir: StagingDir.FullName, Options: CommandUtils.ERunOptions.AllowSpew);
			if (Result.ExitCode != 0)
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
			return FindTagNamesFromFilespec(Parameters.AwsSettings);
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
