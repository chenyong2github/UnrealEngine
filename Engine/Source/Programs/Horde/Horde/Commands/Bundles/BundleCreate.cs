// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using JetBrains.Annotations;
using Microsoft.Extensions.Logging;

namespace Horde.Commands.Bundles
{
	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class BundleCreate : Command
	{
		[CommandLine("-Output=", Required = true, Description = "Output file. Should have a .ref extension.")]
		public FileReference OutputFile { get; set; } = null!;

		[CommandLine("-Input=", Required = true, Description = "Input file or directory")]
		public string Input { get; set; } = null!;

		[CommandLine("-Filter=", Description = "Filter for files to include, in P4 syntax (eg. Foo/...).")]
		public string Filter { get; set; } = "...";

		[CommandLine("-Blobs=", Description = "Directory to store blobs")]
		public DirectoryReference? BlobDir { get; set; }

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			DirectoryReference baseDir;
			List<FileReference> files = new List<FileReference>();

			if (File.Exists(Input))
			{
				FileReference file = new FileReference(Input);
				baseDir = file.Directory;
				files.Add(file);
			}
			else if (Directory.Exists(Input))
			{
				baseDir = new DirectoryReference(Input);
				FileFilter filter = new FileFilter(Filter.Split(';'));
				files.AddRange(filter.ApplyToDirectory(baseDir, true));
			}
			else
			{
				logger.LogError("{Path} does not exist", Input);
				return 1;
			}

			DirectoryReference blobDir = BlobDir ?? OutputFile.Directory;
			DirectoryReference.CreateDirectory(blobDir);

			FileStorageClient store = new FileStorageClient(blobDir, logger);

			Stopwatch timer = Stopwatch.StartNew();

			using (TreeWriter writer = new TreeWriter(store))
			{
				DirectoryNode node = new DirectoryNode(DirectoryFlags.None);

				ChunkingOptions options = new ChunkingOptions();
				await node.CopyFilesAsync(baseDir, files, options, writer, null, CancellationToken.None);

				NodeHandle handle = await writer.FlushAsync(node);

				logger.LogInformation("Writing {File}", OutputFile);
				await FileReference.WriteAllTextAsync(OutputFile, handle.ToString());
			}

			logger.LogInformation("Time: {Time}", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
