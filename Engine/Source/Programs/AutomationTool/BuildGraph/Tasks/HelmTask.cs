// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml;

namespace BuildGraph.Tasks
{
	/// <summary>
	/// Parameters for a Helm task
	/// </summary>
	public class HelmTaskParameters
	{
		/// <summary>
		/// Helm command line arguments
		/// </summary>
		[TaskParameter]
		public string Chart;

		/// <summary>
		/// Name of the release
		/// </summary>
		[TaskParameter]
		public string Deployment;

		/// <summary>
		/// The Kubernetes namespace
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Namespace;

		/// <summary>
		/// The kubectl context
		/// </summary>
		[TaskParameter(Optional = true)]
		public string KubeContext;

		/// <summary>
		/// Values to set for running the chart
		/// </summary>
		[TaskParameter(Optional = true)]
		public List<string> Values;

		/// <summary>
		/// Environment variables to set
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Environment;

		/// <summary>
		/// File to parse environment variables from
		/// </summary>
		[TaskParameter(Optional = true)]
		public string EnvironmentFile;

		/// <summary>
		/// Additional arguments
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments;

		/// <summary>
		/// Base directory for running the command
		/// </summary>
		[TaskParameter(Optional = true)]
		public string WorkingDir;
	}

	/// <summary>
	/// Spawns Helm and waits for it to complete.
	/// </summary>
	[TaskElement("Helm", typeof(HelmTaskParameters))]
	public class HelmTask : SpawnTaskBase
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		HelmTaskParameters Parameters;

		/// <summary>
		/// Construct a Helm task
		/// </summary>
		/// <param name="InParameters">Parameters for the task</param>
		public HelmTask(HelmTaskParameters InParameters)
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
			// Switch Kubernetes config
			if (Parameters.KubeContext != null)
			{
				Execute("kubectl", $"config use-context {Parameters.KubeContext}", WorkingDir: Parameters.WorkingDir);
			}

			// Build the argument list
			List<string> Arguments = new List<string>();
			Arguments.Add("upgrade");
			Arguments.Add(Parameters.Deployment);
			Arguments.Add(new FileReference(Parameters.Chart).FullName);
			Arguments.Add("--install");
			Arguments.Add("--reset-values");
			if(Parameters.Namespace != null)
			{
				Arguments.Add("--namespace");
				Arguments.Add(Parameters.Namespace);
			}
			foreach (string Value in Parameters.Values)
			{
				Arguments.Add("--set");
				Arguments.Add(Value);
			}

			SpawnTaskBase.Execute("helm", CommandLineArguments.Join(Arguments), WorkingDir: Parameters.WorkingDir, EnvVars: ParseEnvVars(Parameters.Environment, Parameters.EnvironmentFile));
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
