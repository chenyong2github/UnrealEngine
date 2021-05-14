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
		/// Path to a credentials file
		/// </summary>
		[TaskParameter(Optional = true)]
		public string CredentialsFile;
	}

	/// <summary>
	/// Spawns Docker and waits for it to complete.
	/// </summary>
	[TaskElement("Docker-Push", typeof(DockerPushTaskParameters))]
	public class DockerPushTask : CustomTask
	{
		/// <summary>
		/// Base class for credentials that can be read from a JSON file
		/// </summary>
		[JsonKnownTypes(typeof(BasicDockerCredentials), typeof(AwsDockerCredentials))]
		abstract class DockerCredentials
		{
			public abstract (string UserName, string Password) Resolve();
		}

		/// <summary>
		/// Basic credentials
		/// </summary>
		[JsonDiscriminator("Basic")]
		class BasicDockerCredentials : DockerCredentials
		{
			public string UserName { get; set; }
			public string Password { get; set; }

			public override (string, string) Resolve() => (UserName, Password);
		}

		/// <summary>
		/// AWS credentials
		/// </summary>
		[JsonDiscriminator("AWS")]
		class AwsDockerCredentials : DockerCredentials
		{
			public string Region { get; set; }
			public string AccessKey { get; set; }
			public string SecretKey { get; set; }

			public override (string, string) Resolve()
			{
				Dictionary<string, string> EnvVars = new Dictionary<string, string>();
				if (AccessKey != null)
				{
					EnvVars.Add("AWS_ACCESS_KEY_ID", AccessKey);
				}
				if(SecretKey != null)
				{
					EnvVars.Add("AWS_SECRET_ACCESS_KEY", SecretKey);
				}

				FileReference AwsExe = CommandUtils.FindToolInPath("aws");
				if (AwsExe == null)
				{
					throw new AutomationException("Unable to find path to AWSCLI. Check you have it installed, and it is on your PATH.");
				}

				StringBuilder Arguments = new StringBuilder("ecr get-login-password");
				if(Region != null)
				{
					Arguments.Append($" --region {Region}");
				}

				IProcessResult Result = CommandUtils.Run(AwsExe.FullName, Arguments.ToString(), Env: EnvVars, Options: CommandUtils.ERunOptions.None);
				if (Result.ExitCode != 0)
				{
					Log.TraceInformation(Result.Output);
					throw new AutomationException("AWSCLI terminated with an exit code indicating an error ({0})", Result.ExitCode);
				}

				return ("AWS", Result.Output);
			}
		}

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
				if (Parameters.CredentialsFile != null)
				{
					FileReference CredentialsFile = ResolveFile(Parameters.CredentialsFile);
					byte[] Data = FileReference.ReadAllBytes(CredentialsFile);

					JsonSerializerOptions Options = new JsonSerializerOptions();
					Options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
					Options.PropertyNameCaseInsensitive = true;
					Options.Converters.Add(new JsonKnownTypesConverterFactory());

					DockerCredentials Credentials = JsonSerializer.Deserialize<DockerCredentials>(Data, Options);
					(string UserName, string Password) = Credentials.Resolve();

					RunDocker(DockerExe, $"login {Parameters.Repository} --username {UserName} --password-stdin", Password, CommandUtils.RootDirectory);
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
			return FindTagNamesFromFilespec(Parameters.CredentialsFile);
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
