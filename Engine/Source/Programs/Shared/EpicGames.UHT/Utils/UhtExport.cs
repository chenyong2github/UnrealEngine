// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Export options.
	/// </summary>
	[Flags]
	public enum UhtExportOptions
	{

		/// <summary>
		/// No export options specified
		/// </summary>
		None = 0,

		/// <summary>
		/// If set, output is to be written
		/// </summary>
		WriteOutput = 1 << 0,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtExportOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtExportOptions InFlags, UhtExportOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtExportOptions InFlags, UhtExportOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtExportOptions InFlags, UhtExportOptions TestFlags, UhtExportOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Represents an output file
	/// </summary>
	public interface IUhtExportOutput
	{

		/// <summary>
		/// Session associated with the output.
		/// </summary>
		public UhtSession Session { get; }

		/// <summary>
		/// Export options
		/// </summary>
		public UhtExportOptions Options { get; set; }

		/// <summary>
		/// Commit the contents of the string builder as the output.
		/// </summary>
		/// <param name="Builder">Source for the content</param>
		/// <returns>A string view of the committed output.  Use this instead of the 
		/// StringBuilder so the builder can be recycled.</returns>
		public StringView CommitOutput(StringBuilder Builder);

		/// <summary>
		/// Commit the value of the string as the output
		/// </summary>
		/// <param name="Output">Output to commit</param>
		public void CommitOutput(string Output);
	}

	/// <summary>
	/// Represents a task to export outputs
	/// </summary>
	public interface IUhtExportTask
	{

		/// <summary>
		/// Session being run
		/// </summary>
		public UhtSession Session { get; }

		/// <summary>
		/// Export options
		/// </summary>
		public UhtExportOptions Options { get; }

		/// <summary>
		/// Task handle associated with the task for it can be waited on
		/// </summary>
		public Task? ActionTask { get; }
	}

	/// <summary>
	/// Delegate used to generate a single output from a task
	/// </summary>
	/// <param name="Output">The single output</param>
	public delegate void UhtExportSingleDelegate(IUhtExportOutput Output);

	/// <summary>
	/// Delegate used to generate two outputs from a task.
	/// </summary>
	/// <param name="HeaderOutput">Header output</param>
	/// <param name="CppOutput">CPP output</param>
	public delegate void UhtExportPairDelegate(IUhtExportOutput HeaderOutput, IUhtExportOutput CppOutput);

	/// <summary>
	/// Factory object used to generate export tasks
	/// </summary>
	public interface IUhtExportFactory
	{
		
		/// <summary>
		/// Session being run
		/// </summary>
		public UhtSession Session { get; }

		/// <summary>
		/// Export options
		/// </summary>
		public UhtExportOptions Options { get; }

		/// <summary>
		/// Create a task to export a single file
		/// </summary>
		/// <param name="Path">Destination file path</param>
		/// <param name="Prereqs">Tasks that must be completed prior to this task running</param>
		/// <param name="AdditionalOptions">Additional options</param>
		/// <param name="Action">Action to be invoked to generate the output</param>
		/// <returns>Task interface.</returns>
		public IUhtExportTask CreateTask(string Path, List<IUhtExportTask> Prereqs, UhtExportOptions AdditionalOptions, UhtExportSingleDelegate Action);

		/// <summary>
		/// Create a task to export two files
		/// </summary>
		/// <param name="HeaderPath">Header destination file path</param>
		/// <param name="CppPath">Cpp destination file path</param>
		/// <param name="Prereqs">Tasks that must be completed prior to this task running</param>
		/// <param name="AdditionalOptions">Additional options</param>
		/// <param name="Action">Action to be invoked to generate the output</param>
		/// <returns>Task interface.</returns>
		public IUhtExportTask CreateTask(string HeaderPath, string CppPath, List<IUhtExportTask> Prereqs, UhtExportOptions AdditionalOptions, UhtExportPairDelegate Action);

		/// <summary>
		/// Make a path for an output based on the header file name.
		/// </summary>
		/// <param name="HeaderFile">Header file being exported.</param>
		/// <param name="Suffix">Suffix to be added to the file name.</param>
		/// <returns>Output file path</returns>
		public string MakePath(UhtHeaderFile HeaderFile, string Suffix);

		/// <summary>
		/// Make a path for an output based on the package name.
		/// </summary>
		/// <param name="Package">Package being exported</param>
		/// <param name="Suffix">Suffix to be added to the file name.</param>
		/// <returns>Output file path</returns>
		public string MakePath(UhtPackage Package, string Suffix);
	}
}
