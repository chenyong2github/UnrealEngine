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

namespace HordeAgent.Commands.Archive
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Archive", "Create", "Creates an archive containing a set of files or folders")]
	class CreateArchiveCommand : Command
	{
		/// <summary>
		/// Input path. May be a local filesystem wildcard (eg. "E:\P4\...") or Perforce depot path.
		/// </summary>
		[CommandLine("-Source=", Required = true)]
		public string SourcePath = null!;

		/// <summary>
		/// Path to a base file to generate an archive from
		/// </summary>
		[CommandLine("-Base=")]
		public FileReference? BasePath = null;

		/// <summary>
		/// Output path to an index file.
		/// </summary>
		[CommandLine("-Target=", Required = true)]
		public FileReference TargetPath = null!;

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			TreePackOptions Options = new TreePackOptions();
//			Options.MaxBlobSize = 64 * 1024;
//			Options.MaxInlineBlobSize = 20 * 1024;

			DirectoryReference TargetDir = TargetPath.Directory;
			NamespaceId NamespaceId = new NamespaceId("test");
			FileStorageClient Storage = new FileStorageClient(TargetDir, Logger);

			TreePack TargetPack = new TreePack(Storage, NamespaceId, Options);

			TreePackDirWriter TargetWriter = new TreePackDirWriter(TargetPack);
			if (BasePath != null)
			{
				IoHash RootHash = IoHash.Parse((await FileReference.ReadAllTextAsync(BasePath)).Trim());
				byte[] RootObject = await FileReference.ReadAllBytesAsync(BasePath.ChangeExtension(".dat"));
				TargetPack.AddRootBlob(RootObject);

				ReadOnlyMemory<byte> RootDirData = await TargetPack.GetDataAsync(RootHash);
				TreePackDirNode RootDirNode = TreePackDirNode.Parse(RootDirData);

				TargetWriter = new TreePackDirWriter(TargetPack, RootDirNode);
			}

			if (SourcePath.StartsWith("//"))
			{
				await ReadFilesFromPerforceAsync(SourcePath, TargetPack, TargetWriter);
			}
			else
			{
				await ReadFilesFromDiskAsync(SourcePath, TargetPack, TargetWriter);
			}

			IoHash NewHash = await TargetWriter.FlushAsync();
			TreePackObject Pack = await TargetPack.FlushAsync(NewHash, DateTime.UtcNow);
			await WriteSummaryFiles(NamespaceId, Pack, new HashSet<IoHash>(), Storage, TargetDir);

			Logger.LogInformation("Writing root to {TargetPath}", TargetPath);
			await FileReference.WriteAllTextAsync(TargetPath, NewHash.ToString());

			Logger.LogInformation("Writing pack to {TargetPath}", TargetPath.ChangeExtension(".dat"));
			await FileReference.WriteAllBytesAsync(TargetPath.ChangeExtension(".dat"), CbSerializer.Serialize(Pack).GetView().ToArray());

			Logger.LogInformation("Writing yaml to {TargetPath}", TargetPath.ChangeExtension(".yml"));
			TreePack.WriteSummary(TargetPath.ChangeExtension(".yml"), Pack);

			return 0;
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
