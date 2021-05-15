// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Xml;

namespace AutomationTool.Tasks
{
	#region Settings

	/// <summary>
	/// AWS Settings file
	/// </summary>
	public class AwsSettings
	{
		/// <summary>
		/// The default region
		/// </summary>
		public string Region { get; set; }

		/// <summary>
		/// The credentials
		/// </summary>
		public AwsCredentials Credentials { get; set; }

		/// <summary>
		/// Read settings from a file
		/// </summary>
		/// <param name="File"></param>
		/// <returns></returns>
		public static AwsSettings Read(FileReference File)
		{
			byte[] Data = FileReference.ReadAllBytes(File);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.PropertyNameCaseInsensitive = true;

			return JsonSerializer.Deserialize<AwsSettings>(Data, Options);
		}

		/// <summary>
		/// Gets environment variables in a dictionary
		/// </summary>
		/// <param name="EnvVars"></param>
		public void GetEnvVars(Dictionary<string, string> EnvVars)
		{
			if (Region != null)
			{
				EnvVars.Add("AWS_DEFAULT_REGION", Region);
			}
			if (Credentials != null)
			{
				if (Credentials.AccessKeyId != null)
				{
					EnvVars.Add("AWS_ACCESS_KEY_ID", Credentials.AccessKeyId);
				}
				if (Credentials.SecretAccessKey != null)
				{
					EnvVars.Add("AWS_SECRET_ACCESS_KEY", Credentials.SecretAccessKey);
				}
				if (Credentials.SessionToken != null)
				{
					EnvVars.Add("AWS_SESSION_TOKEN", Credentials.SessionToken);
				}
			}
		}
	}

	/// <summary>
	/// Credentials for AWS
	/// </summary>
	public class AwsCredentials
	{
		/// <summary>
		/// Access key for AWS
		/// </summary>
		public string AccessKeyId { get; set; }

		/// <summary>
		/// Secret key for AWS
		/// </summary>
		public string SecretAccessKey { get; set; }

		/// <summary>
		/// Session token for AWS
		/// </summary>
		public string SessionToken { get; set; }
	}

	#endregion

	/// <summary>
	/// Parameters for an AWS CLI task
	/// </summary>
	public class AwsTaskParameters
	{
		/// <summary>
		/// AWS command line arguments
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments;

		/// <summary>
		/// Whether or not to show the console output from the AWS CLI command.
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Verbose = false;

		/// <summary>
		/// Path to a file containing the credentials needed to run the AWS command
		/// </summary>
		[TaskParameter(Optional = true)]
		public string SettingsFile;

		/// <summary>
		/// Path to receive the ouptut
		/// </summary>
		[TaskParameter(Optional = true)]
		public string OutputFile;
	}

	/// <summary>
	/// Spawns AWS CLI and waits for it to complete.
	/// </summary>
	[TaskElement("Aws", typeof(AwsTaskParameters))]
	public class AwsTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		AwsTaskParameters Parameters;

		/// <summary>
		/// Construct an AWS CLI task
		/// </summary>
		/// <param name="InParameters">Parameters for the task</param>
		public AwsTask(AwsTaskParameters InParameters)
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
			FileReference SettingsFile = (Parameters.SettingsFile != null) ? ResolveFile(Parameters.SettingsFile) : null;
			FileReference OutputFile = (Parameters.OutputFile != null) ? ResolveFile(Parameters.OutputFile) : null;
			Run(Parameters.Arguments, SettingsFile: SettingsFile, OutputFile: OutputFile, LogOutput: Parameters.Verbose);
		}

		/// <summary>
		/// Runs an AWS command
		/// </summary>
		/// <param name="Arguments"></param>
		/// <param name="SettingsFile"></param>
		/// <param name="LogOutput"></param>
		/// <param name="OutputFile"></param>
		/// <param name="BaseDir"></param>
		public static string Run(string Arguments, FileReference SettingsFile = null, FileReference OutputFile = null, DirectoryReference BaseDir = null, bool LogOutput = true)
		{
			FileReference ToolFile = CommandUtils.FindToolInPath("aws");
			if (ToolFile == null)
			{
				throw new AutomationException("Unable to find path to AWS CLI. Check you have it installed, and it is on your PATH.");
			}

			Dictionary<string, string> EnvVars = new Dictionary<string, string>();
			if (SettingsFile != null)
			{
				AwsSettings Settings = AwsSettings.Read(SettingsFile);
				Settings.GetEnvVars(EnvVars);
			}

			CommandUtils.ERunOptions Options = CommandUtils.ERunOptions.AppMustExist;
			if (LogOutput)
			{
				Options |= CommandUtils.ERunOptions.AllowSpew;
			}
			IProcessResult Result = CommandUtils.Run(ToolFile.FullName, Arguments, WorkingDir: BaseDir?.FullName, Env: EnvVars, Options: Options);
			if (OutputFile != null)
			{
				FileReference.WriteAllText(OutputFile, Result.Output);
			}
			if (Result.ExitCode != 0)
			{
				throw new AutomationException("AWS terminated with an exit code indicating an error ({0})", Result.ExitCode);
			}
			return Result.Output;
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
