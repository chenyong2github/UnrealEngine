// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Interface for toolchain operations that produce output
	/// </summary>
	interface IActionGraphBuilder
	{
		/// <summary>
		/// Creates a new action to be built as part of this target
		/// </summary>
		/// <param name="Type">Type of action to create</param>
		/// <returns>New action</returns>
		Action CreateAction(ActionType Type);

		/// <summary>
		/// Creates a response file for use in the action graph
		/// </summary>
		/// <param name="Location">Location of the response file</param>
		/// <param name="Contents">Contents of the file</param>
		/// <returns>New file item</returns>
		FileItem CreateIntermediateTextFile(FileReference Location, string Contents);

		/// <summary>
		/// Adds a file which is in the non-unity working set
		/// </summary>
		/// <param name="File">The file to add to the working set</param>
		void AddFileToWorkingSet(FileItem File);

		/// <summary>
		/// Adds a file which is a candidate for being in the non-unity working set
		/// </summary>
		/// <param name="File">The file to add to the working set</param>
		void AddCandidateForWorkingSet(FileItem File);

		/// <summary>
		/// Adds a source directory. These folders are scanned recursively for C++ source files.
		/// </summary>
		/// <param name="SourceDir">Base source directory</param>
		void AddSourceDir(DirectoryItem SourceDir);

		/// <summary>
		/// Adds the given source files as dependencies
		/// </summary>
		/// <param name="SourceDir">Source directory containing files to build</param>
		/// <param name="SourceFiles">Contents of the directory</param>
		void AddSourceFiles(DirectoryItem SourceDir, FileItem[] SourceFiles);

		/// <summary>
		/// Sets the output items which belong to a particular module
		/// </summary>
		/// <param name="ModuleName">Name of the module</param>
		/// <param name="OutputItems">Array of output items for this module</param>
		void SetOutputItemsForModule(string ModuleName, FileItem[] OutputItems);

		/// <summary>
		/// Adds a diagnostic message
		/// </summary>
		/// <param name="Message">Message to display</param>
		void AddDiagnostic(string Message);
	}

	/// <summary>
	/// Implementation of IActionGraphBuilder which discards all unnecessary operations
	/// </summary>
	class NullActionGraphBuilder : IActionGraphBuilder
	{
		/// <inheritdoc/>
		public Action CreateAction(ActionType Type)
		{
			return new Action(Type);
		}

		/// <inheritdoc/>
		public virtual FileItem CreateIntermediateTextFile(FileReference Location, string Contents)
		{
			Utils.WriteFileIfChanged(Location, Contents, StringComparison.OrdinalIgnoreCase);
			return FileItem.GetItemByFileReference(Location);
		}

		/// <inheritdoc/>
		public virtual void AddSourceDir(DirectoryItem SourceDir)
		{
		}

		/// <inheritdoc/>
		public virtual void AddSourceFiles(DirectoryItem SourceDir, FileItem[] SourceFiles)
		{
		}

		/// <inheritdoc/>
		public virtual void AddFileToWorkingSet(FileItem File)
		{
		}

		/// <inheritdoc/>
		public virtual void AddCandidateForWorkingSet(FileItem File)
		{
		}

		/// <inheritdoc/>
		public virtual void AddDiagnostic(string Message)
		{
		}

		/// <inheritdoc/>
		public virtual void SetOutputItemsForModule(string ModuleName, FileItem[] OutputItems)
		{
		}
	}

	/// <summary>
	/// Implementation of IActionGraphBuilder which forwards calls to an underlying implementation, allowing derived classes to intercept certain calls
	/// </summary>
	class ForwardingActionGraphBuilder : IActionGraphBuilder
	{
		/// <summary>
		/// The inner graph builder
		/// </summary>
		IActionGraphBuilder Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">Builder to pass all calls to</param>
		public ForwardingActionGraphBuilder(IActionGraphBuilder Inner)
		{
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public virtual Action CreateAction(ActionType Type)
		{
			return Inner.CreateAction(Type);
		}

		/// <inheritdoc/>
		public virtual FileItem CreateIntermediateTextFile(FileReference Location, string Contents)
		{
			return Inner.CreateIntermediateTextFile(Location, Contents);
		}

		/// <inheritdoc/>
		public virtual void AddSourceDir(DirectoryItem SourceDir)
		{
			Inner.AddSourceDir(SourceDir);
		}

		/// <inheritdoc/>
		public virtual void AddSourceFiles(DirectoryItem SourceDir, FileItem[] SourceFiles)
		{
			Inner.AddSourceFiles(SourceDir, SourceFiles);
		}

		/// <inheritdoc/>
		public virtual void AddFileToWorkingSet(FileItem File)
		{
			Inner.AddFileToWorkingSet(File);
		}

		/// <inheritdoc/>
		public virtual void AddCandidateForWorkingSet(FileItem File)
		{
			Inner.AddCandidateForWorkingSet(File);
		}

		/// <inheritdoc/>
		public virtual void AddDiagnostic(string Message)
		{
			Inner.AddDiagnostic(Message);
		}

		/// <inheritdoc/>
		public virtual void SetOutputItemsForModule(string ModuleName, FileItem[] OutputItems)
		{
			Inner.SetOutputItemsForModule(ModuleName, OutputItems);
		}
	}

	/// <summary>
	/// Extension methods for IActionGraphBuilder classes
	/// </summary>
	static class ActionGraphBuilderExtensions
	{
		/// <summary>
		/// Creates an action which copies a file from one location to another
		/// </summary>
		/// <param name="Graph">The action graph</param>
		/// <param name="SourceFile">The source file location</param>
		/// <param name="TargetFile">The target file location</param>
		/// <returns>File item for the output file</returns>
		public static Action CreateCopyAction(this IActionGraphBuilder Graph, FileItem SourceFile, FileItem TargetFile)
		{
			Action CopyAction = Graph.CreateAction(ActionType.BuildProject);
			CopyAction.CommandDescription = "Copy";
			CopyAction.CommandPath = BuildHostPlatform.Current.Shell;
			if (BuildHostPlatform.Current.ShellType == ShellType.Cmd)
			{
				CopyAction.CommandArguments = String.Format("/C \"copy /Y \"{0}\" \"{1}\" 1>nul\"", SourceFile.AbsolutePath, TargetFile.AbsolutePath);
			}
			else
			{
				CopyAction.CommandArguments = String.Format("-c 'cp -f \"{0}\" \"{1}\"'", SourceFile.AbsolutePath, TargetFile.AbsolutePath);
			}
			CopyAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
			CopyAction.PrerequisiteItems.Add(SourceFile);
			CopyAction.ProducedItems.Add(TargetFile);
			CopyAction.DeleteItems.Add(TargetFile);
			CopyAction.StatusDescription = TargetFile.Location.GetFileName();
			CopyAction.bCanExecuteRemotely = false;
			return CopyAction;
		}

		/// <summary>
		/// Creates an action which copies a file from one location to another
		/// </summary>
		/// <param name="Graph">List of actions to be executed. Additional actions will be added to this list.</param>
		/// <param name="SourceFile">The source file location</param>
		/// <param name="TargetFile">The target file location</param>
		/// <returns>File item for the output file</returns>
		public static FileItem CreateCopyAction(this IActionGraphBuilder Graph, FileReference SourceFile, FileReference TargetFile)
		{
			FileItem SourceFileItem = FileItem.GetItemByFileReference(SourceFile);
			FileItem TargetFileItem = FileItem.GetItemByFileReference(TargetFile);

			Graph.CreateCopyAction(SourceFileItem, TargetFileItem);

			return TargetFileItem;
		}

		/// <summary>
		/// Creates an action which calls UBT recursively
		/// </summary>
		/// <param name="Graph">The action graph</param>
		/// <param name="Type">Type of the action</param>
		/// <param name="Arguments">Arguments for the action</param>
		/// <returns>New action instance</returns>
		public static Action CreateRecursiveAction<T>(this IActionGraphBuilder Graph, ActionType Type, string Arguments) where T : ToolMode
		{
			ToolModeAttribute Attribute = typeof(T).GetCustomAttribute<ToolModeAttribute>();
			if (Attribute == null)
			{
				throw new BuildException("Missing ToolModeAttribute on {0}", typeof(T).Name);
			}

			Action NewAction = Graph.CreateAction(Type);
			NewAction.CommandPath = UnrealBuildTool.GetUBTPath();
			NewAction.CommandArguments = String.Format("-Mode={0} {1}", Attribute.Name, Arguments);
			return NewAction;
		}

		/// <summary>
		/// Creates a text file with the given contents.  If the contents of the text file aren't changed, it won't write the new contents to
		/// the file to avoid causing an action to be considered outdated.
		/// </summary>
		/// <param name="Graph">The action graph</param>
		/// <param name="AbsolutePath">Path to the intermediate file to create</param>
		/// <param name="Contents">Contents of the new file</param>
		/// <returns>File item for the newly created file</returns>
		public static FileItem CreateIntermediateTextFile(this IActionGraphBuilder Graph, FileReference AbsolutePath, IEnumerable<string> Contents)
		{
			return Graph.CreateIntermediateTextFile(AbsolutePath, string.Join(Environment.NewLine, Contents));
		}
	}
}
