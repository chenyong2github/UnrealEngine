// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;
using UnrealBuildBase;
using UnrealBuildTool;
using System.Reflection;
using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;
using System.Runtime.CompilerServices;
using System.ComponentModel;

#nullable enable

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a task which calls a C# method
	/// </summary>
	public class CallMethodTaskParameters
	{
		/// <summary>
		/// Name of the class to run a method in
		/// </summary>
		[TaskParameter]
		public string Class = String.Empty;

		/// <summary>
		/// The method name to execute.
		/// </summary>
		[TaskParameter]
		public string Method = String.Empty;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg1;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg2;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg3;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg4;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg5;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg6;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg7;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg8;

		/// <summary>
		/// Argument value
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Arg9;

		/// <summary>
		/// List of tags for outputs
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Tags;
	}

	class BgContextImpl : BgContext
	{
		JobContext JobContext;

		public BgContextImpl(JobContext JobContext, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
			: base(CreateExprContext(TagNameToFileSet))
		{
			this.JobContext = JobContext;
		}

		static BgExprContext CreateExprContext(Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			BgExprContext Context = new BgExprContext();
			foreach (KeyValuePair<string, HashSet<FileReference>> Pair in TagNameToFileSet)
			{
				Context.TagNameToFileSet[Pair.Key] = FileSet.FromFiles(Unreal.RootDirectory, Pair.Value);
			}
			return Context;
		}

		public override string Stream => CommandUtils.P4Enabled ? CommandUtils.P4Env.Branch : "";

		public override int Change => CommandUtils.P4Enabled ? CommandUtils.P4Env.Changelist : 0;

		public override int CodeChange => CommandUtils.P4Enabled ? CommandUtils.P4Env.CodeChangelist : 0;

		public override (int Major, int Minor, int Patch) EngineVersion
		{
			get
			{
				ReadOnlyBuildVersion Current = ReadOnlyBuildVersion.Current;
				return (Current.MajorVersion, Current.MinorVersion, Current.PatchVersion);
			}
		}

		public override bool IsBuildMachine => CommandUtils.IsBuildMachine;
	}

	/// <summary>
	/// Invokes a C# method.
	/// </summary>
	[TaskElement("CallMethod", typeof(CallMethodTaskParameters))]
	public class CallMethodTask : BgTaskImpl
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		CallMethodTaskParameters Parameters;

		/// <summary>
		/// Construct a new CommandletTask.
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public CallMethodTask(CallMethodTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override async Task ExecuteAsync(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			Type? Type = Type.GetType(Parameters.Class);
			if (Type == null)
			{
				throw new AutomationException($"Unable to find class '{Parameters.Class}'");
			}

			MethodInfo? Method = Type.GetMethod(Parameters.Method, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
			if (Method == null)
			{
				throw new AutomationException($"Unable to find method '{Parameters.Method}' on class {Parameters.Class}");
			}

			List<string?> ArgumentStrings = new List<string?>();
			ArgumentStrings.Add(Parameters.Arg1!);
			ArgumentStrings.Add(Parameters.Arg2!);
			ArgumentStrings.Add(Parameters.Arg3!);
			ArgumentStrings.Add(Parameters.Arg4!);
			ArgumentStrings.Add(Parameters.Arg5!);
			ArgumentStrings.Add(Parameters.Arg6!);
			ArgumentStrings.Add(Parameters.Arg7!);
			ArgumentStrings.Add(Parameters.Arg8!);
			ArgumentStrings.Add(Parameters.Arg9!);

			ParameterInfo[] MethodParameters = Method.GetParameters();

			BgContextImpl State = new BgContextImpl(Job, TagNameToFileSet);

			object[] Arguments = new object[MethodParameters.Length];
			for (int Idx = 0; Idx < MethodParameters.Length; Idx++)
			{
				string? ArgumentString = ArgumentStrings[Idx];

				Type ParameterType = MethodParameters[Idx].ParameterType;
				if (ParameterType == typeof(BgContext))
				{
					Arguments[Idx] = State;
				}
				else if (ArgumentString == null)
				{
					throw new AutomationException($"Missing argument for parameter {MethodParameters[Idx].Name}");
				}
				else if (ParameterType == typeof(bool))
				{
					Arguments[Idx] = bool.Parse(ArgumentString);
				}
				else if (ParameterType == typeof(int))
				{
					Arguments[Idx] = int.Parse(ArgumentString);
				}
				else if (ParameterType == typeof(string))
				{
					Arguments[Idx] = ArgumentString;
				}
				else if (typeof(IBgExpr).IsAssignableFrom(ParameterType))
				{
					Arguments[Idx] = BgType.Get(ParameterType).DeserializeArgument(ArgumentString);
				}
				else
				{
					Arguments[Idx] = TypeDescriptor.GetConverter(ParameterType).ConvertFromString(ArgumentString)!;
				}
			}

			Task Task = (Task)Method.Invoke(null, Arguments)!;
			await Task;

			if(Parameters.Tags != null)
			{
				object? Result = null;
				if (Method.ReturnType.IsGenericType && Method.ReturnType.GetGenericTypeDefinition() == typeof(Task<>))
				{
					Type TaskType = Task.GetType();
					PropertyInfo? Property = TaskType.GetProperty(nameof(Task<int>.Result));
					Result = Property!.GetValue(Task);
				}

				object?[] OutputValues;
				if (Result is BgFileSet)
				{
					OutputValues = new[] { Result };
				}
				else if (Result is ITuple Tuple)
				{
					OutputValues = Enumerable.Range(0, Tuple.Length).Select(x => Tuple[x]).ToArray();
				}
				else
				{
					OutputValues = Array.Empty<object?>();
				}

				string[] TagNames = Parameters.Tags.Split(';');
				for (int Idx = 0; Idx < OutputValues.Length; Idx++)
				{
					if (OutputValues[Idx] is BgFileSet Token)
					{
						string TagName = TagNames[Idx];
						FileSet FileSet = Token.ComputeValue(State.Context);
						TagNameToFileSet[TagName] = new HashSet<FileReference>(FileSet.Flatten().Values);
						BuildProducts.UnionWith(TagNameToFileSet[TagName]);
					}
				}
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
