// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Xml;

namespace AutomationTool
{
	/// <summary>
	/// A task invocation
	/// </summary>
	public class TaskInfo
	{
		/// <summary>
		/// Line number in a source file that this task was declared. Optional; used for log messages.
		/// </summary>
		public Tuple<FileReference, int> SourceLocation
		{
			get;
			set;
		}

		/// <summary>
		/// Name of the task
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Arguments for the task
		/// </summary>
		public Dictionary<string, string> Arguments { get; } = new Dictionary<string, string>();

		/// <summary>
		/// Constructor
		/// </summary>
		public TaskInfo(Tuple<FileReference, int> SourceLocation, string Name)
		{
			this.SourceLocation = SourceLocation;
			this.Name = Name;
		}

		/// <summary>
		/// Write to an xml file
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(XmlWriter Writer)
		{
			Writer.WriteStartElement(Name);
			foreach (KeyValuePair<string, string> Argument in Arguments)
			{
				Writer.WriteAttributeString(Argument.Key, Argument.Value);
			}
			Writer.WriteEndElement();
		}
	}

	/// <summary>
	/// Extension methods for ILogger
	/// </summary>
	static class TaskInfoExtensions
	{
		internal static void LogError(this ILogger Logger, TaskInfo TaskInfo, string Message, params object[] Args)
		{
			LogError(Logger, TaskInfo.SourceLocation.Item1, TaskInfo.SourceLocation.Item2, Message, Args);
		}

		internal static void LogWarning(this ILogger Logger, TaskInfo TaskInfo, string Message, params object[] Args)
		{
			LogWarning(Logger, TaskInfo.SourceLocation.Item1, TaskInfo.SourceLocation.Item2, Message, Args);
		}

		internal static void LogError(this ILogger Logger, FileReference File, int LineNumber, string Message, params object[] Args)
		{
			object[] AllArgs = new object[Args.Length + 2];
			AllArgs[0] = File;
			AllArgs[1] = LineNumber;
			Args.CopyTo(AllArgs, 2);
			Logger.LogError($"{{Script}}({{Line}}): {Message}", AllArgs);
		}

		internal static void LogWarning(this ILogger Logger, FileReference File, int LineNumber, string Message, params object[] Args)
		{
			object[] AllArgs = new object[Args.Length + 2];
			AllArgs[0] = File;
			AllArgs[1] = LineNumber;
			Args.CopyTo(AllArgs, 2);
			Logger.LogWarning($"{{Script}}({{Line}}): {Message}", AllArgs);
		}
	}
}
