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
	/// Represents a task to export outputs
	/// </summary>
	public interface IUhtExportTask
	{
		/// <summary>
		/// Factory associated with the task
		/// </summary>
		public IUhtExportFactory Factory { get; }

		/// <summary>
		/// Session being run
		/// </summary>
		public UhtSession Session { get; }

		/// <summary>
		/// Task handle associated with the task for it can be waited on
		/// </summary>
		public Task? ActionTask { get; }

		/// <summary>
		/// Commit the contents of the string builder as the output.
		/// If you have a string builder, use this method so that a 
		/// temporary buffer can be used.
		/// </summary>
		/// <param name="FilePath">Destination file path</param>
		/// <param name="Builder">Source for the content</param>
		public void CommitOutput(string FilePath, StringBuilder Builder);

		/// <summary>
		/// Commit the value of the string as the output
		/// </summary>
		/// <param name="FilePath">Destination file path</param>
		/// <param name="Output">Output to commit</param>
		public void CommitOutput(string FilePath, StringView Output);
	}

	/// <summary>
	/// Delegate invoked by a task
	/// </summary>
	/// <param name="Task">Invoking task</param>
	public delegate void UhtExportTaskDelegate(IUhtExportTask Task);

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
		/// Create a task
		/// </summary>
		/// <param name="Prereqs">Tasks that must be completed prior to this task running</param>
		/// <param name="Action">Action to be invoked to generate the output(s)</param>
		/// <returns>Task interface.</returns>
		public IUhtExportTask CreateTask(List<IUhtExportTask>? Prereqs, UhtExportTaskDelegate Action);

		/// <summary>
		/// Create a task
		/// </summary>
		/// <param name="Action">Action to be invoked to generate the output(s)</param>
		/// <returns>Task interface.</returns>
		public IUhtExportTask CreateTask(UhtExportTaskDelegate Action);

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

		/// <summary>
		/// Make a path for an output based on the package output directory.
		/// </summary>
		/// <param name="Package">Destination package</param>
		/// <param name="FileName">Name of the file</param>
		/// <param name="Extension">Extension to add to the file</param>
		/// <returns>Output file path</returns>
		public string MakePath(UhtPackage Package, string FileName, string Extension);
	}
}
