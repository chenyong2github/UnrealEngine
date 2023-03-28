// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.IO.Pipelines;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Flags for a file entry
	/// </summary>
	[Flags]
	public enum FileEntryFlags
	{
		/// <summary>
		/// No other flags set
		/// </summary>
		None = 0,

		/// <summary>
		/// Indicates that the referenced file is executable
		/// </summary>
		Executable = 4,

		/// <summary>
		/// File should be stored as read-only
		/// </summary>
		ReadOnly = 8,

		/// <summary>
		/// File contents are utf-8 encoded text. Client may want to replace line-endings with OS-specific format.
		/// </summary>
		Text = 16,

		/// <summary>
		/// Used to indicate that custom data is included in the output. Used internally for serialization; not exposed to users.
		/// </summary>
		HasCustomData = 32,

		/// <summary>
		/// File should be materialized as UTF-16 (but is stored as a UTF-8 source)
		/// </summary>
		Utf16 = 64,
	}

	/// <summary>
	/// Entry for a file within a directory node
	/// </summary>
	public sealed class FileEntry : TreeNodeRef<FileNode>
	{
		/// <summary>
		/// Name of this file
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Flags for this file
		/// </summary>
		public FileEntryFlags Flags { get; set; }

		/// <summary>
		/// Length of this entry
		/// </summary>
		public long Length { get; }

		/// <summary>
		/// Hash of the target node
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Custom user data for this file entry
		/// </summary>
		public ReadOnlyMemory<byte> CustomData { get; set; } = ReadOnlyMemory<byte>.Empty;

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(Utf8String name, FileEntryFlags flags, long length, NodeHandle node)
			: base(node)
		{
			Name = name;
			Flags = flags;
			Length = length;
			Hash = node.Hash;
		}

		/// <summary>
		/// Deserialize from a buffer
		/// </summary>
		/// <param name="reader"></param>
		public FileEntry(ITreeNodeReader reader)
			: base(reader)
		{
			Name = reader.ReadUtf8String();
			Flags = (FileEntryFlags)reader.ReadUnsignedVarInt();
			Length = (long)reader.ReadUnsignedVarInt();
			Hash = reader.ReadIoHash();

			if ((Flags & FileEntryFlags.HasCustomData) != 0)
			{
				CustomData = reader.ReadVariableLengthBytes();
				Flags &= ~FileEntryFlags.HasCustomData;
			}
		}

		/// <summary>
		/// Serialize this entry
		/// </summary>
		/// <param name="writer"></param>
		public override void Serialize(ITreeNodeWriter writer)
		{
			base.Serialize(writer);

			FileEntryFlags flags = (CustomData.Length > 0) ? (Flags | FileEntryFlags.HasCustomData) : (Flags & ~FileEntryFlags.HasCustomData);

			writer.WriteUtf8String(Name);
			writer.WriteUnsignedVarInt((ulong)flags);
			writer.WriteUnsignedVarInt((ulong)Length);
			writer.WriteIoHash(Hash);

			if ((flags & FileEntryFlags.HasCustomData) != 0)
			{
				writer.WriteVariableLengthBytes(CustomData.Span);
			}
		}

		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="reader">Reader for retrieving existing node data</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			FileNode node = await ExpandAsync(reader, cancellationToken);
			await node.CopyToStreamAsync(reader, outputStream, cancellationToken);
		}

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="reader">Reader for retrieving existing node data</param>
		/// <param name="file">File to write with the contents of this node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(TreeReader reader, FileInfo file, CancellationToken cancellationToken)
		{
			FileNode node = await ExpandAsync(reader, cancellationToken);
			await node.CopyToFileAsync(reader, file, cancellationToken);
		}

		/// <inheritdoc/>
		public override string ToString() => Name.ToString();
	}

	/// <summary>
	/// Reference to a directory node, including the target hash and length
	/// </summary>
	public class DirectoryNodeRef : TreeNodeRef<DirectoryNode>
	{
		/// <summary>
		/// Length of this directory tree
		/// </summary>
		public long Length => (Target == null) ? _cachedLength : Target.Length;

		/// <summary>
		/// Hash of the target node
		/// </summary>
		public IoHash Hash { get; private set; }

		/// <summary>
		/// Cached value for the length of this tree
		/// </summary>
		long _cachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryNodeRef(DirectoryNode node)
			: base(node)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="reader"></param>
		public DirectoryNodeRef(ITreeNodeReader reader)
			: base(reader)
		{
			_cachedLength = (long)reader.ReadUnsignedVarInt();
			Hash = reader.ReadIoHash();
		}

		/// <summary>
		/// Serialize this directory entry to disk
		/// </summary>
		/// <param name="writer"></param>
		public override void Serialize(ITreeNodeWriter writer)
		{
			base.Serialize(writer);

			writer.WriteUnsignedVarInt((ulong)Length);
			writer.WriteIoHash(Hash);
		}

		/// <inheritdoc/>
		protected override void OnCollapse()
		{
			base.OnCollapse();

			Hash = Target!.Hash;
			_cachedLength = Target!.Length;
		}
	}

	/// <summary>
	/// Entry for a directory within a directory node
	/// </summary>
	public class DirectoryEntry : DirectoryNodeRef
	{
		/// <summary>
		/// Name of this directory
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(Utf8String name)
			: this(name, new DirectoryNode())
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(Utf8String name, DirectoryNode node)
			: base(node)
		{
			Name = name;
		}

		/// <summary>
		/// Deserializing constructor
		/// </summary>
		/// <param name="reader"></param>
		public DirectoryEntry(ITreeNodeReader reader)
			: base(reader)
		{
			Name = reader.ReadUtf8String();
		}

		/// <summary>
		/// Serialize this directory entry to disk
		/// </summary>
		/// <param name="writer"></param>
		public override void Serialize(ITreeNodeWriter writer)
		{
			base.Serialize(writer);

			writer.WriteUtf8String(Name);
		}

		/// <inheritdoc/>
		public override string ToString() => Name.ToString();
	}

	/// <summary>
	/// Stats reported for copy operations
	/// </summary>
	public interface ICopyStats
	{
		/// <summary>
		/// Number of files that have been copied
		/// </summary>
		int CopiedCount { get; }

		/// <summary>
		/// Total size of data to be copied
		/// </summary>
		long CopiedSize { get; }

		/// <summary>
		/// Total number of files to copy
		/// </summary>
		int TotalCount { get; }

		/// <summary>
		/// Total size of data to copy
		/// </summary>
		long TotalSize { get; }
	}

	/// <summary>
	/// Progress logger for writing copy stats
	/// </summary>
	public class CopyStatsLogger : IProgress<ICopyStats>
	{
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public CopyStatsLogger(ILogger logger) => _logger = logger;

		/// <inheritdoc/>
		public void Report(ICopyStats stats)
		{
			_logger.LogInformation("Copied {NumFiles:n0}/{TotalFiles:n0} ({Size:n1}/{TotalSize:n1}mb, {Pct}%)", stats.CopiedCount, stats.TotalCount, stats.CopiedSize / (1024.0 * 1024.0), stats.TotalSize / (1024.0 * 1024.0), (int)((Math.Max(stats.CopiedCount, 1) * 100) / Math.Max(stats.TotalCount, 1)));
		}
	}

	/// <summary>
	/// Flags for a directory node
	/// </summary>
	public enum DirectoryFlags
	{
		/// <summary>
		/// No flags specified
		/// </summary>
		None = 0,
	}

	/// <summary>
	/// A directory node
	/// </summary>
	[TreeNode("{0714EC11-291A-4D07-867F-E78AD6809979}", 1)]
	public class DirectoryNode : TreeNode
	{
		readonly Dictionary<Utf8String, FileEntry> _nameToFileEntry = new Dictionary<Utf8String, FileEntry>();
		readonly Dictionary<Utf8String, DirectoryEntry> _nameToDirectoryEntry = new Dictionary<Utf8String, DirectoryEntry>();

		/// <summary>
		/// Total size of this directory
		/// </summary>
		public long Length => _nameToFileEntry.Values.Sum(x => x.Length) + _nameToDirectoryEntry.Values.Sum(x => x.Length);

		/// <summary>
		/// Flags for this directory 
		/// </summary>
		public DirectoryFlags Flags { get; }

		/// <summary>
		/// All the files within this directory
		/// </summary>
		public IReadOnlyCollection<FileEntry> Files => _nameToFileEntry.Values;

		/// <summary>
		/// Map of name to file entry
		/// </summary>
		public IReadOnlyDictionary<Utf8String, FileEntry> NameToFile => _nameToFileEntry;

		/// <summary>
		/// All the subdirectories within this directory
		/// </summary>
		public IReadOnlyCollection<DirectoryEntry> Directories => _nameToDirectoryEntry.Values;

		/// <summary>
		/// Map of name to file entry
		/// </summary>
		public IReadOnlyDictionary<Utf8String, DirectoryEntry> NameToDirectory => _nameToDirectoryEntry;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="flags"></param>
		public DirectoryNode(DirectoryFlags flags = DirectoryFlags.None)
		{
			Flags = flags;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public DirectoryNode(ITreeNodeReader reader)
		{
			Flags = (DirectoryFlags)reader.ReadUnsignedVarInt();

			int fileCount = (int)reader.ReadUnsignedVarInt();
			_nameToFileEntry.EnsureCapacity(fileCount);

			for (int idx = 0; idx < fileCount; idx++)
			{
				FileEntry entry = new FileEntry(reader);
				_nameToFileEntry[entry.Name] = entry;
			}

			int directoryCount = (int)reader.ReadUnsignedVarInt();
			_nameToDirectoryEntry.EnsureCapacity(directoryCount);
			
			for (int idx = 0; idx < directoryCount; idx++)
			{
				DirectoryEntry entry = new DirectoryEntry(reader);
				_nameToDirectoryEntry[entry.Name] = entry;
			}
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUnsignedVarInt((ulong)Flags);

			writer.WriteUnsignedVarInt(Files.Count);
			foreach (FileEntry fileEntry in _nameToFileEntry.Values)
			{
				writer.WriteRef(fileEntry);
			}

			writer.WriteUnsignedVarInt(Directories.Count);
			foreach (DirectoryEntry directoryEntry in _nameToDirectoryEntry.Values)
			{
				writer.WriteRef(directoryEntry);
			}
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs()
		{
			foreach (FileEntry fileEntry in _nameToFileEntry.Values)
			{
				yield return fileEntry;
			}
			foreach (DirectoryEntry directoryEntry in _nameToDirectoryEntry.Values)
			{
				yield return directoryEntry;
			}
		}

		/// <summary>
		/// Clear the contents of this directory
		/// </summary>
		public void Clear()
		{
			_nameToFileEntry.Clear();
			_nameToDirectoryEntry.Clear();
			MarkAsDirty();
		}

		/// <summary>
		/// Check whether an entry with the given name exists in this directory
		/// </summary>
		/// <param name="name">Name of the entry to search for</param>
		/// <returns>True if the entry exists</returns>
		public bool Contains(Utf8String name) => TryGetFileEntry(name, out _) || TryGetDirectoryEntry(name, out _);

		#region File operations

		/// <summary>
		/// Adds a new file entry to this directory
		/// </summary>
		/// <param name="entry">The entry to add</param>
		public void AddFile(FileEntry entry)
		{
			_nameToFileEntry[entry.Name] = entry;
			MarkAsDirty();
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="length">Length of the file</param>
		/// <param name="handle">Handle to the file data</param>
		/// <returns>The new directory object</returns>
		public FileEntry AddFile(Utf8String name, FileEntryFlags flags, long length, NodeHandle handle)
		{
			FileEntry entry = new FileEntry(name, flags, length, handle);
			AddFile(entry);
			return entry;
		}

		/// <summary>
		/// Finds or adds a file with the given path
		/// </summary>
		/// <param name="reader">Reader for node data</param>
		/// <param name="path">Path to the file</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="handle">The file node</param>
		/// <param name="length">Length of the node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<FileEntry> AddFileByPathAsync(TreeReader reader, Utf8String path, FileEntryFlags flags, long length, NodeHandle handle, CancellationToken cancellationToken = default)
		{
			DirectoryNode directory = this;

			Utf8String remainingPath = path;
			if (remainingPath[0] == '/' || remainingPath[0] == '\\')
			{
				remainingPath = remainingPath.Substring(1);
			}

			for (; ; )
			{
				int pathLength = 0;
				for (; ; pathLength++)
				{
					if (pathLength == remainingPath.Length)
					{
						return directory.AddFile(remainingPath, flags, length, handle);
					}

					byte character = remainingPath[pathLength];
					if (character == '\\' || character == '/')
					{
						break;
					}
				}

				if (pathLength > 0)
				{
					directory = await directory.FindOrAddDirectoryAsync(reader, remainingPath.Slice(0, pathLength), cancellationToken);
				}
				remainingPath = remainingPath.Slice(pathLength + 1);
			}
		}

		/// <summary>
		/// Attempts to get a file entry with the given name
		/// </summary>
		/// <param name="name">Name of the file</param>
		/// <returns>Entry for the given name</returns>
		public FileEntry GetFileEntry(Utf8String name) => _nameToFileEntry[name];

		/// <summary>
		/// Attempts to get a file entry with the given name
		/// </summary>
		/// <param name="name">Name of the file</param>
		/// <param name="entry">Entry for the file</param>
		/// <returns>True if the file was found</returns>
		public bool TryGetFileEntry(Utf8String name, [NotNullWhen(true)] out FileEntry? entry) => _nameToFileEntry.TryGetValue(name, out entry);

		/// <summary>
		/// Deletes the file entry with the given name
		/// </summary>
		/// <param name="name">Name of the entry to delete</param>
		/// <returns>True if the entry was found, false otherwise</returns>
		public bool DeleteFile(Utf8String name)
		{
			if (_nameToFileEntry.Remove(name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		/// <summary>
		/// Deletes a file with the given path
		/// </summary>
		/// <param name="reader">Reader for existing node data</param>
		/// <param name="path"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async ValueTask<bool> DeleteFileByPathAsync(TreeReader reader, Utf8String path, CancellationToken cancellationToken)
		{
			Utf8String remainingPath = path;
			for (DirectoryNode? directory = this; directory != null;)
			{
				int length = remainingPath.IndexOf('/');
				if (length == -1)
				{
					return directory.DeleteFile(remainingPath);
				}
				if (length > 0)
				{
					directory = await directory.FindDirectoryAsync(reader, remainingPath.Slice(0, length), cancellationToken);
				}
				remainingPath = remainingPath.Slice(length + 1);
			}
			return false;
		}

		#endregion

		#region Directory operations

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="entry">Name of the new directory</param>
		public void AddDirectory(DirectoryEntry entry)
		{
			if (TryGetFileEntry(entry.Name, out _))
			{
				throw new ArgumentException($"A file with the name '{entry.Name}' already exists in this directory", nameof(entry));
			}

			_nameToDirectoryEntry.Add(entry.Name, entry);
			MarkAsDirty();
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public DirectoryNode AddDirectory(Utf8String name)
		{
			DirectoryNode node = new DirectoryNode(Flags);
			AddDirectory(new DirectoryEntry(name, node));
			return node;
		}

		/// <summary>
		/// Get a directory entry with the given name
		/// </summary>
		/// <param name="name">Name of the directory</param>
		/// <returns>The entry with the given name</returns>
		public DirectoryEntry GetDirectoryEntry(Utf8String name) => _nameToDirectoryEntry[name];

		/// <summary>
		/// Attempts to get a directory entry with the given name
		/// </summary>
		/// <param name="name">Name of the directory</param>
		/// <param name="entry">Entry for the directory</param>
		/// <returns>True if the directory was found</returns>
		public bool TryGetDirectoryEntry(Utf8String name, [NotNullWhen(true)] out DirectoryEntry? entry) => _nameToDirectoryEntry.TryGetValue(name, out entry);

		/// <summary>
		/// Attempts to get a directory entry with the given name, and adds one if it does not already exist.
		/// </summary>
		/// <param name="name">Name of the directory</param>
		/// <returns>Directory entry with the given name</returns>
		public DirectoryEntry FindOrAddDirectoryEntry(Utf8String name)
		{
			DirectoryEntry? entry;
			if (!_nameToDirectoryEntry.TryGetValue(name, out entry))
			{
				entry = new DirectoryEntry(name);
				_nameToDirectoryEntry.Add(name, entry);
				MarkAsDirty();
			}
			return entry;
		}

		/// <summary>
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="reader">Reader for node data</param>
		/// <param name="name">Name of the new directory</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode?> FindDirectoryAsync(TreeReader reader, Utf8String name, CancellationToken cancellationToken)
		{
			if (TryGetDirectoryEntry(name, out DirectoryEntry? entry))
			{
				return await entry.ExpandAsync(reader, cancellationToken);
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="reader">Reader for node data</param>
		/// <param name="name">Name of the new directory</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode> FindOrAddDirectoryAsync(TreeReader reader, Utf8String name, CancellationToken cancellationToken)
		{
			DirectoryNode? directory = await FindDirectoryAsync(reader, name, cancellationToken);
			if (directory == null)
			{
				directory = AddDirectory(name);
			}
			return directory;
		}

		/// <summary>
		/// Deletes the file entry with the given name
		/// </summary>
		/// <param name="name">Name of the entry to delete</param>
		/// <returns>True if the entry was found, false otherwise</returns>
		public bool DeleteDirectory(Utf8String name)
		{
			if (_nameToDirectoryEntry.Remove(name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		#endregion

		/// <summary>
		/// Reports progress info back to callers
		/// </summary>
		class CopyStats : ICopyStats
		{
			readonly object _lockObject = new object();
			readonly Stopwatch _timer = Stopwatch.StartNew();
			readonly IProgress<ICopyStats> _progress;

			public int CopiedCount { get; set; }
			public long CopiedSize { get; set; }
			public int TotalCount { get; }
			public long TotalSize { get; }

			public CopyStats(int totalCount, long totalSize, IProgress<ICopyStats> progress)
			{
				TotalCount = totalCount;
				TotalSize = totalSize;
				_progress = progress;
			}

			public void Update(int count, long size)
			{
				lock (_lockObject)
				{
					CopiedCount += count;
					CopiedSize += size;
					if (_timer.Elapsed > TimeSpan.FromSeconds(10.0))
					{
						_progress.Report(this);
						_timer.Restart();
					}
				}
			}

			public void Flush()
			{
				lock (_lockObject)
				{
					_progress.Report(this);
					_timer.Restart();
				}
			}
		}

		/// <summary>
		/// Adds files from a flat list of paths
		/// </summary>
		/// <param name="baseDir">Base directory to base paths relative to</param>
		/// <param name="files">Files to add</param>
		/// <param name="options">Options for chunking file content</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="progress">Feedback interface for progress updates</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task CopyFilesAsync(DirectoryReference baseDir, IEnumerable<FileReference> files, ChunkingOptions options, TreeWriter writer, IProgress<ICopyStats>? progress, CancellationToken cancellationToken)
		{
			Dictionary<DirectoryReference, DirectoryNode> dirToNode = new Dictionary<DirectoryReference, DirectoryNode>();
			dirToNode.Add(baseDir, this);

			List<(DirectoryNode, FileInfo)> groupedFiles = new List<(DirectoryNode, FileInfo)>();
			foreach (FileReference file in files.OrderBy(x => x))
			{
				DirectoryNode? node = FindOrAddDirectory(file.Directory, baseDir, dirToNode);
				if (node == null)
				{
					throw new InvalidOperationException($"File {file} is not under base directory {baseDir}");
				}
				groupedFiles.Add((node, file.ToFileInfo()));
			}

			await CopyFromDirectoryAsync(groupedFiles, options, writer, progress, cancellationToken);
		}

		static DirectoryNode? FindOrAddDirectory(DirectoryReference dir, DirectoryReference baseDir, Dictionary<DirectoryReference, DirectoryNode> dirToNode)
		{
			DirectoryNode? node;
			if (!dirToNode.TryGetValue(dir, out node))
			{
				DirectoryReference? parentDir = dir.ParentDirectory;
				if (parentDir != null)
				{
					DirectoryNode? parentNode = FindOrAddDirectory(parentDir, baseDir, dirToNode);
					if (parentNode != null)
					{
						node = parentNode.AddDirectory(dir.GetDirectoryName());
						dirToNode.Add(dir, node);
					}
				}
			}
			return node;
		}

		/// <summary>
		/// Copies entries from a zip file
		/// </summary>
		/// <param name="stream">Input stream</param>
		/// <param name="writer">Writer for new nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task CopyFromZipStreamAsync(Stream stream, TreeWriter writer, CancellationToken cancellationToken)
		{
			FileNodeWriter fileWriter = new FileNodeWriter(writer, new ChunkingOptions());
			using (ZipArchive archive = new ZipArchive(stream, ZipArchiveMode.Read, true))
			{
				foreach (ZipArchiveEntry entry in archive.Entries)
				{
					if (entry.Name.Length > 0)
					{
						NodeHandle node;
						using (Stream entryStream = entry.Open())
						{
							node = await fileWriter.CreateAsync(entryStream, cancellationToken);
						}

						string fullName = entry.FullName;
						int slashIdx = fullName.LastIndexOf('/');

						DirectoryNode directory;
						if (slashIdx == -1)
						{
							directory = this;
						}
						else
						{
							directory = await FindOrAddDirectoryAsync(null!, fullName.Substring(0, slashIdx), cancellationToken);
						}

						FileEntryFlags flags = FileEntryFlags.None;
						if ((entry.ExternalAttributes & (0b_001_001_001 << 16)) != 0)
						{
							flags |= FileEntryFlags.Executable;
						}

						FileEntry fileEntry = new FileEntry(entry.Name, flags, fileWriter.Length, node);
						directory.AddFile(fileEntry);
					}
				}
			}
		}

		/// <summary>
		/// Adds files from a directory on disk
		/// </summary>
		/// <param name="directoryInfo"></param>
		/// <param name="options">Options for chunking file content</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="progress">Feedback interface for progress updates</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task CopyFromDirectoryAsync(DirectoryInfo directoryInfo, ChunkingOptions options, TreeWriter writer, IProgress<ICopyStats>? progress, CancellationToken cancellationToken = default)
		{
			// Enumerate all the files below this directory
			List<(DirectoryNode DirectoryNode, FileInfo FileInfo)> files = new List<(DirectoryNode, FileInfo)>();
			FindFilesToCopy(directoryInfo, files);
			await CopyFromDirectoryAsync(files, options, writer, progress, cancellationToken);
		}

		/// <summary>
		/// Adds files from a directory on disk
		/// </summary>
		/// <param name="files"></param>
		/// <param name="options">Options for chunking file content</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="progress">Progress notification object</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task CopyFromDirectoryAsync(List<(DirectoryNode DirectoryNode, FileInfo FileInfo)> files, ChunkingOptions options, TreeWriter writer, IProgress<ICopyStats>? progress, CancellationToken cancellationToken = default)
		{
			const int MaxWriters = 32;
			const long MinSizePerWriter = 1024 * 1024;

			// Compute the total size
			long totalSize = files.Sum(x => x.Item2.Length);
			long chunkSize = Math.Max(MinSizePerWriter, totalSize / MaxWriters);

			// Create the progress reporting object
			CopyStats? copyStats = null;
			if (progress != null)
			{
				copyStats = new CopyStats(files.Count, totalSize, progress);
			}

			List<Task> tasks = new List<Task>();
			long currentSize = 0;
			long targetSize = chunkSize;
			FileEntry[] fileEntries = new FileEntry[files.Count];

			// Split it into separate writers
			for (int minIdx = 0; minIdx < files.Count; )
			{
				currentSize += files[minIdx].FileInfo.Length;

				int maxIdx = minIdx + 1;
				while (maxIdx < files.Count && currentSize + files[maxIdx].FileInfo.Length <= targetSize)
				{
					currentSize += files[maxIdx].FileInfo.Length;
					maxIdx++;
				}

				int minIdxCopy = minIdx;
				tasks.Add(Task.Run(() => CopyFilesAsync(files, minIdxCopy, maxIdx, fileEntries, options, writer, copyStats, cancellationToken), cancellationToken));

				targetSize += chunkSize;
				minIdx = maxIdx;
			}

			// Wait for them all to finish
			await Task.WhenAll(tasks);

			// Update the directory with all the output entries
			for (int idx = 0; idx < files.Count; idx++)
			{
				files[idx].DirectoryNode.AddFile(fileEntries[idx]);
			}

			// Write the final stats
			copyStats?.Flush();
		}

		void FindFilesToCopy(DirectoryInfo directoryInfo, List<(DirectoryNode, FileInfo)> files)
		{
			foreach (DirectoryInfo subDirectoryInfo in directoryInfo.EnumerateDirectories())
			{
				AddDirectory(subDirectoryInfo.Name).FindFilesToCopy(subDirectoryInfo, files);
			}
			foreach (FileInfo fileInfo in directoryInfo.EnumerateFiles())
			{
				files.Add((this, fileInfo));
			}
		}

		static async Task CopyFilesAsync(List<(DirectoryNode DirectoryNode, FileInfo FileInfo)> files, int minIdx, int maxIdx, FileEntry[] entries, ChunkingOptions options, TreeWriter baseWriter, CopyStats? copyStats, CancellationToken cancellationToken)
		{
			TreeWriter writer = baseWriter;
			try
			{
				if (minIdx != 0)
				{
					writer = new TreeWriter(baseWriter);
				}

				FileNodeWriter fileNodeWriter = new FileNodeWriter(writer, options);
				for (int idx = minIdx; idx < maxIdx; idx++)
				{
					FileInfo fileInfo = files[idx].FileInfo;
					NodeHandle handle = await fileNodeWriter.CreateAsync(fileInfo, cancellationToken);
					entries[idx] = new FileEntry(fileInfo.Name, FileEntryFlags.None, fileNodeWriter.Length, handle);
					copyStats?.Update(1, fileNodeWriter.Length);
				}

				if (minIdx != 0)
				{
					await writer.FlushAsync(cancellationToken);
				}
			}
			finally
			{
				if (minIdx != 0)
				{
					writer.Dispose();
				}
			}
		}

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="reader">Reader to retrieve data from</param>
		/// <param name="directoryInfo"></param>
		/// <param name="logger"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task CopyToDirectoryAsync(TreeReader reader, DirectoryInfo directoryInfo, ILogger logger, CancellationToken cancellationToken)
		{
			directoryInfo.Create();

			List<Task> tasks = new List<Task>();
			foreach (FileEntry fileEntry in _nameToFileEntry.Values)
			{
				FileInfo fileInfo = new FileInfo(Path.Combine(directoryInfo.FullName, fileEntry.Name.ToString()));
				FileNode fileNode = await fileEntry.ExpandAsync(reader, cancellationToken);
				tasks.Add(Task.Run(() => fileNode.CopyToFileAsync(reader, fileInfo, cancellationToken), cancellationToken));
			}
			foreach (DirectoryEntry directoryEntry in _nameToDirectoryEntry.Values)
			{
				DirectoryInfo subDirectoryInfo = directoryInfo.CreateSubdirectory(directoryEntry.Name.ToString());
				DirectoryNode subDirectoryNode = await directoryEntry.ExpandAsync(reader, cancellationToken);
				tasks.Add(Task.Run(() => subDirectoryNode.CopyToDirectoryAsync(reader, subDirectoryInfo, logger, cancellationToken), cancellationToken));
			}

			await Task.WhenAll(tasks);
		}

		/// <summary>
		/// Returns a stream containing the zipped contents of this directory
		/// </summary>
		/// <param name="reader">Reader for other nodes</param>
		/// <returns>Stream containing zipped archive data</returns>
		public Stream ToZipStream(TreeReader reader) => new ZippedDirectoryStream(reader, this);
	}

	/// <summary>
	/// Stream which zips a directory node tree dynamically
	/// </summary>
	class ZippedDirectoryStream : Stream
	{
		/// <inheritdoc/>
		public override bool CanRead => true;

		/// <inheritdoc/>
		public override bool CanSeek => false;

		/// <inheritdoc/>
		public override bool CanWrite => false;

		/// <inheritdoc/>
		public override long Length => throw new NotImplementedException();

		/// <inheritdoc/>
		public override long Position { get => _position; set => throw new NotImplementedException(); }

		readonly Pipe _pipe;
		readonly BackgroundTask _backgroundTask;

		long _position;
		ReadOnlySequence<byte> _current = ReadOnlySequence<byte>.Empty;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="reader">Reader to read nodes through</param>
		/// <param name="node">Root node to copy from</param>
		public ZippedDirectoryStream(TreeReader reader, DirectoryNode node)
		{
			_pipe = new Pipe();
			_backgroundTask = BackgroundTask.StartNew(ctx => CopyToPipeAsync(reader, node, _pipe.Writer, ctx));
		}

		/// <inheritdoc/>
		public override async ValueTask DisposeAsync()
		{
			await base.DisposeAsync();

			await _backgroundTask.DisposeAsync();
		}

		/// <inheritdoc/>
		public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
		{
			while (_current.Length == 0)
			{
				ReadResult result = await _pipe.Reader.ReadAsync(cancellationToken);
				_current = result.Buffer;

				if (result.IsCompleted && _current.Length == 0)
				{
					return 0;
				}
			}

			int initialSize = buffer.Length;
			while (buffer.Length > 0 && _current.Length > 0)
			{
				int copy = Math.Min(buffer.Length, _current.First.Length);
				_current.First.Slice(0, copy).CopyTo(buffer);
				_current = _current.Slice(copy);
				buffer = buffer.Slice(copy);
			}

			if (_current.Length == 0)
			{
				_pipe.Reader.AdvanceTo(_current.End);
			}

			int length = initialSize - buffer.Length;
			_position += length;
			return length;
		}

		static async Task CopyToPipeAsync(TreeReader reader, DirectoryNode node, PipeWriter writer, CancellationToken cancellationToken)
		{
			using Stream outputStream = writer.AsStream();
			using ZipArchive archive = new ZipArchive(outputStream, ZipArchiveMode.Create);
			await CopyFilesAsync(reader, node, "", archive, cancellationToken);
		}

		static async Task CopyFilesAsync(TreeReader reader, DirectoryNode directory, string prefix, ZipArchive archive, CancellationToken cancellationToken)
		{
			foreach (DirectoryEntry directoryEntry in directory.Directories)
			{
				DirectoryNode node = await directoryEntry.ExpandAsync(reader, cancellationToken);
				await CopyFilesAsync(reader, node, $"{prefix}{directoryEntry.Name}/", archive, cancellationToken);
			}

			foreach (FileEntry fileEntry in directory.Files)
			{
				ZipArchiveEntry entry = archive.CreateEntry($"{prefix}{fileEntry}");

				if ((fileEntry.Flags & FileEntryFlags.Executable) != 0)
				{
					entry.ExternalAttributes |= 0b_111_111_101 << 16; // rwx rwx r-x
				}
				else
				{
					entry.ExternalAttributes |= 0b_110_110_100 << 16; // rw- rw- r--
				}

				using Stream entryStream = entry.Open();
				await fileEntry.CopyToStreamAsync(reader, entryStream, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public override void Flush()
		{
		}

		/// <inheritdoc/>
		public override int Read(byte[] buffer, int offset, int count) => ReadAsync(buffer.AsMemory(offset, count)).AsTask().Result;

		/// <inheritdoc/>
		public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();

		/// <inheritdoc/>
		public override void SetLength(long value) => throw new NotSupportedException();

		/// <inheritdoc/>
		public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
	}
}
