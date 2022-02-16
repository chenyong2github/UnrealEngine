// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using System.Text.Json;
using HordeAgent.Services;
using Grpc.Net.Client;
using System.Net.Http.Headers;
using Grpc.Core;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Serilog.Events;
using EpicGames.Serialization;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using System.Net.Http;
using EpicGames.Horde.Compute.Impl;
using EpicGames.Horde.Storage.Impl;
using System.Threading;
using EpicGames.Perforce;
using System.Diagnostics;

namespace HordeAgent.Commands.Archive
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Archive", "Extract", "Creates an archive containing a set of files or folders")]
	class ExtractArchiveCommand : Command
	{
		/// <summary>
		/// Input path.
		/// </summary>
		[CommandLine("-Input=", Required = true)]
		public FileReference InputPath = null!;

		/// <summary>
		/// Output path to an index file.
		/// </summary>
		[CommandLine("-Output=", Required = true)]
		public DirectoryReference TargetDir = null!;

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			TreePackOptions Options = new TreePackOptions();
			Options.MaxBlobSize = 64 * 1024;
			Options.MaxInlineBlobSize = 20 * 1024;

			DirectoryReference Inputdir = InputPath.Directory;
			NamespaceId NamespaceId = new NamespaceId("test");
			FileStorageClient Storage = new FileStorageClient(Inputdir, Logger);

			TreePack TreePack = new TreePack(Storage, NamespaceId, Options);

			IoHash RootHash = IoHash.Parse((await FileReference.ReadAllTextAsync(InputPath)).Trim());
			byte[] RootObject = await FileReference.ReadAllBytesAsync(InputPath.ChangeExtension(".dat"));
			TreePack.AddRootBlob(RootObject);

			await ExtractFilesAsync(TreePack, RootHash, TargetDir, Logger);
			return 0;
		}

		async Task ExtractFilesAsync(TreePack TreePack, IoHash Hash, DirectoryReference TargetDir, ILogger Logger)
		{
			Logger.LogInformation("Writing directory {TargetDir}...", TargetDir);
			DirectoryReference.CreateDirectory(TargetDir);

			ReadOnlyMemory<byte> Data = await TreePack.GetDataAsync(Hash);
			Debug.Assert(Data.Span[0] == (byte)TreePackNodeType.Directory);

			TreePackDirNode Dir = TreePackDirNode.Parse(Data);
			foreach (TreePackDirEntry Entry in Dir.Entries)
			{
				if ((Entry.Flags & TreePackDirEntryFlags.Directory) != 0)
				{
					DirectoryReference TargetSubDir = DirectoryReference.Combine(TargetDir, Entry.Name.ToString());
					await ExtractFilesAsync(TreePack, Entry.Hash, TargetSubDir, Logger);
				}
				else if ((Entry.Flags & TreePackDirEntryFlags.File) != 0)
				{
					FileReference File = FileReference.Combine(TargetDir, Entry.Name.ToString());
					Logger.LogInformation("Writing {File}...", File);

					using (FileStream Stream = FileReference.Open(File, FileMode.Create, FileAccess.Write, FileShare.Read))
					{
						await ExtractFileAsync(TreePack, Entry.Hash, Stream, Logger);
					}
				}
				else
				{
					throw new InvalidDataException($"Unknown entry type for {Entry.Name}");
				}
			}
		}

		async Task ExtractFileAsync(TreePack TreePack, IoHash Hash, Stream OutputStream, ILogger Logger)
		{
			ReadOnlyMemory<byte> Data = await TreePack.GetDataAsync(Hash);
			TreePackNodeType Type = (TreePackNodeType)Data.Span[0];

			if (Type == TreePackNodeType.Binary)
			{
				Logger.LogInformation("Copying data from binary node {Hash}", Hash);
				await OutputStream.WriteAsync(Data.Slice(1));
			}
			else if (Type == TreePackNodeType.Concat)
			{
				Logger.LogInformation("Start data from concat node {Hash}", Hash);

				TreePackConcatNode Concat = TreePackConcatNode.Parse(Data.Span);
				foreach (TreePackConcatEntry Entry in Concat.Entries)
				{
					await ExtractFileAsync(TreePack, Entry.Hash, OutputStream, Logger);
				}

				Logger.LogInformation("Finish data from concat node {Hash}", Hash);
			}
			else
			{
				throw new InvalidDataException();
			}
		}

		async Task WriteSummaryFiles(NamespaceId NamespaceId, TreePackObject RootObject, HashSet<IoHash> VisitedObjects, IStorageClient StorageClient, DirectoryReference TargetDir)
		{
			foreach (TreePackObjectImport Import in RootObject.ObjectImports)
			{
				if (VisitedObjects.Add(Import.Object.Hash))
				{
					TreePackObject Object = await StorageClient.ReadObjectAsync<TreePackObject>(NamespaceId, Import.Object.Hash);
					TreePack.WriteSummary(FileReference.Combine(TargetDir, $"{NamespaceId}-{Import.Object.Hash}.yml"), Object);
					await WriteSummaryFiles(NamespaceId, Object, VisitedObjects, StorageClient, TargetDir);
				}
			}
		}

		public async Task ReadFilesFromPerforceAsync(string SourcePath, TreePack TargetPack, TreePackDirWriter TargetWriter)
		{
			PerforceSettings Settings = new PerforceSettings(PerforceSettings.Default);
			Settings.PreferNativeClient = true;

			using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(Settings, Log.Logger);
			ClientRecord Client = await Perforce.GetClientAsync(null);

			Dictionary<int, (string Path, TreePackFileWriter Writer)> Files = new Dictionary<int, (string, TreePackFileWriter)>();
			await foreach (PerforceResponse Response in Perforce.StreamCommandAsync("sync", new string[] { "-f" }, new string[] { $"{SourcePath}#have" }, null, typeof(SyncRecord), true, default))
			{
				PerforceIo? Io = Response.Io;
				if (Io != null)
				{
					if (Io.Command == PerforceIoCommand.Open)
					{
						string Path = GetClientRelativePath(Io.Payload, Client.Root);
						Files[Io.File] = (Path, new TreePackFileWriter(TargetPack));
					}
					else if (Io.Command == PerforceIoCommand.Write)
					{
						TreePackFileWriter FileWriter = Files[Io.File].Writer;
						await FileWriter.WriteAsync(Io.Payload, false);
					}
					else if (Io.Command == PerforceIoCommand.Close)
					{
						(string Path, TreePackFileWriter FileWriter) = Files[Io.File];
						IoHash FileHash = await FileWriter.FinalizeAsync();
						await TargetWriter.FindOrAddFileByPathAsync(Path, TreePackDirEntryFlags.File, FileHash, Sha1Hash.Zero);
					}
				}
			}
		}

		static string GetClientRelativePath(ReadOnlyMemory<byte> Data, string ClientRoot)
		{
			ReadOnlySpan<byte> Span = Data.Span;

			string Path = Encoding.UTF8.GetString(Span.Slice(0, Span.IndexOf((byte)0)));
			if (!Path.StartsWith(ClientRoot))
			{
				throw new ArgumentException($"Unable to make path {Path} relative to client root {ClientRoot}");
			}

			return Path.Substring(ClientRoot.Length).Replace('\\', '/').TrimStart('/');
		}

		public async Task ReadFilesFromDiskAsync(string SourcePath, TreePack TargetPack, TreePackDirWriter TargetWriter)
		{
			int MaxIdx = FileFilter.FindWildcardIndex(SourcePath);
			if (MaxIdx < 0)
			{
				MaxIdx = SourcePath.Length - 1;
			}
			while (SourcePath[MaxIdx] != Path.DirectorySeparatorChar)
			{
				MaxIdx--;
			}

			DirectoryReference BaseDir = new DirectoryReference(SourcePath.Substring(0, MaxIdx));
			string SearchPath = SourcePath.Substring(MaxIdx + 1);

			foreach (FileReference File in FileFilter.ResolveWildcard(BaseDir, SearchPath))
			{
				TreePackFileWriter FileWriter = new TreePackFileWriter(TargetPack);

				byte[] Buffer = new byte[65536];
				using (FileStream Stream = FileReference.Open(File, FileMode.Open, FileAccess.Read))
				{
					for (; ; )
					{
						int ReadBytes = await Stream.ReadAsync(Buffer);
						if (ReadBytes == 0)
						{
							break;
						}
						await FileWriter.WriteAsync(Buffer.AsMemory(0, ReadBytes), false);
					}
				}

				IoHash Hash = await FileWriter.FinalizeAsync();

				string Path = File.MakeRelativeTo(BaseDir).Replace('\\', '/');
				await TargetWriter.FindOrAddFileByPathAsync(Path, TreePackDirEntryFlags.File, Hash);
			}
		}
	}
}
