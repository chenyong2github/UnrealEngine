// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Action = Build.Bazel.Remote.Execution.V2.Action;
using Digest = Build.Bazel.Remote.Execution.V2.Digest;
using Directory = Build.Bazel.Remote.Execution.V2.Directory;
using RpcCommand = Build.Bazel.Remote.Execution.V2.Command;

namespace EpicGames.Horde.Common.RemoteExecution
{
	public class JsonFileNode
	{
		public string Name { get; set; } = String.Empty;
		public string LocalFile { get; set; } = String.Empty;

		public async Task<FileNode> BuildAsync(UploadList UploadList)
		{
			FileNode Node = new FileNode();
			Node.Name = String.IsNullOrEmpty(Name) ? Path.GetFileName(LocalFile) : Name;
			Node.Digest = await UploadList.AddFileAsync(LocalFile);
			return Node;
		}
	}

	public class JsonDirectory
	{
		public List<JsonFileNode> Files { get; set; } = new List<JsonFileNode>();
		public List<JsonDirectoryNode> Directories { get; set; } = new List<JsonDirectoryNode>();

		public async Task<Digest> BuildAsync(UploadList UploadList)
		{
			Directory Directory = new Directory();
			foreach (JsonFileNode File in Files)
			{
				Directory.Files.Add(await File.BuildAsync(UploadList));
			}
			foreach (JsonDirectoryNode DirectoryNode in Directories)
			{
				Directory.Directories.Add(await DirectoryNode.BuildAsync(UploadList));
			}
			return await UploadList.AddAsync(Directory);
		}
	}

	public class JsonDirectoryNode : JsonDirectory
	{
		public string Name { get; set; } = String.Empty;

		public new async Task<DirectoryNode> BuildAsync(UploadList UploadList)
		{
			DirectoryNode Node = new DirectoryNode();
			Node.Name = Name;
			Node.Digest = await base.BuildAsync(UploadList);
			return Node;
		}
	}

	public class JsonCommand
	{
		public List<string> OutputPaths { get; set; } = new List<string>();
		public List<string> Arguments { get; set; } = new List<string>();
		public Dictionary<string, string> EnvVars { get; set; } = new Dictionary<string, string>();
		public string WorkingDirectory { get; set; } = String.Empty;

		public async Task<Digest> BuildAsync(UploadList UploadList)
		{
			RpcCommand Command = new RpcCommand();
			Command.Arguments.Add(Arguments);
			Command.WorkingDirectory = WorkingDirectory;
			Command.OutputPaths.Add(OutputPaths);

			foreach (KeyValuePair<string, string> Pair in EnvVars)
			{
				Command.EnvironmentVariables.Add(new RpcCommand.Types.EnvironmentVariable { Name = Pair.Key, Value = Pair.Value });
			}

			return await UploadList.AddAsync(Command);
		}
	}

	public class JsonAction
	{
		public JsonCommand Command { get; set; } = new JsonCommand();
		public JsonDirectory Workspace { get; set; } = new JsonDirectory();

		public async Task<Digest> BuildAsync(UploadList UploadList, ByteString? Salt, bool DoNotCache)
		{
			Action NewAction = new Action();
			NewAction.CommandDigest = await Command.BuildAsync(UploadList);
			NewAction.InputRootDigest = await Workspace.BuildAsync(UploadList);
			NewAction.Salt = Salt;
			NewAction.DoNotCache = DoNotCache;
			return await UploadList.AddAsync(NewAction);
		}
	}
}
