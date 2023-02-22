// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using System.Collections.Generic;

namespace EpicGames.Horde.Compute.Cpp
{
	/// <summary>
	/// Describes an action to be executed in a particular workspace
	/// </summary>
	[TreeNode("{43D16B98-1AD5-4BB2-8BD9-EF0CA3635BE0}", 1)]
	public class CppComputeNode : TreeNode
	{
		/// <summary>
		/// The executable to run
		/// </summary>
		public string Executable { get; set; }

		/// <summary>
		/// List of command line arguments for the process to run.
		/// </summary>
		public List<string> Arguments { get; } = new List<string>();

		/// <summary>
		/// Environment variables to set for the child process
		/// </summary>
		public Dictionary<string, string> EnvVars { get; } = new Dictionary<string, string>();

		/// <summary>
		/// Path to the working directory within the workspace
		/// </summary>
		public string WorkingDirectory { get; set; }

		/// <summary>
		/// Reference to a sandbox 
		/// </summary>
		public TreeNodeRef<DirectoryNode> Sandbox { get; set; }

		/// <summary>
		/// List of output paths to be captured on completion of the action. These may be files or directories.
		/// </summary>
		public List<string> OutputPaths { get; } = new List<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="executable">The executable to run</param>
		/// <param name="arguments">Arguments for the executable to run</param>
		/// <param name="workingDirectory">Working directory for execution</param>
		/// <param name="sandbox">Hash of the sandbox</param>
		public CppComputeNode(string executable, List<string> arguments, string workingDirectory, TreeNodeRef<DirectoryNode> sandbox)
		{
			Executable = executable;
			Arguments = arguments;
			WorkingDirectory = workingDirectory;
			Sandbox = sandbox;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public CppComputeNode(ITreeNodeReader reader)
		{
			Executable = reader.ReadString();
			Arguments = reader.ReadList(MemoryReaderExtensions.ReadString);
			EnvVars = reader.ReadDictionary(MemoryReaderExtensions.ReadString, MemoryReaderExtensions.ReadString);
			WorkingDirectory = reader.ReadString();
			Sandbox = reader.ReadRef<DirectoryNode>();
			OutputPaths = reader.ReadList(MemoryReaderExtensions.ReadString);
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUtf8String(Executable);
			writer.WriteList(Arguments, MemoryWriterExtensions.WriteString);
			writer.WriteDictionary(EnvVars, MemoryWriterExtensions.WriteString, MemoryWriterExtensions.WriteString);
			writer.WriteUtf8String(WorkingDirectory);
			writer.WriteRef(Sandbox);
			writer.WriteList(OutputPaths, MemoryWriterExtensions.WriteString);
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs()
		{
			yield return Sandbox;
		}
	}
}
