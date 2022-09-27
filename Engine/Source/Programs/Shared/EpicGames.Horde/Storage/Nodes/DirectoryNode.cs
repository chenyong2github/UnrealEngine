// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

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
		/// The data for this entry is a Perforce depot path and revision rather than the actual file contents.
		/// </summary>
		PerforceDepotPathAndRevision = 32,
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
		public long Length => (Target == null) ? _cachedLength : Target.Length;

		/// <summary>
		/// SHA1 hash of this file, with Git prefix
		/// </summary>
		public Sha1Hash GitHash { get; set; }

		/// <summary>
		/// Cached length of this node
		/// </summary>
		readonly long _cachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(Utf8String name, FileEntryFlags flags)
			: this(name, flags, new LeafFileNode())
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(Utf8String name, FileEntryFlags flags, FileNode node)
			: base(node)
		{
			Name = name;
			Flags = flags;
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

			_cachedLength = (long)reader.ReadUnsignedVarInt();
		}

		/// <summary>
		/// Serialize this entry
		/// </summary>
		/// <param name="writer"></param>
		public new void Serialize(ITreeNodeWriter writer)
		{
			base.Serialize(writer);

			writer.WriteUtf8String(Name);
			writer.WriteUnsignedVarInt((ulong)Flags);
			writer.WriteUnsignedVarInt((ulong)Length);
		}

		/// <summary>
		/// Appends data to this file
		/// </summary>
		/// <param name="data">Data to append to the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task AppendAsync(ReadOnlyMemory<byte> data, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			FileNode node = await ExpandAsync(cancellationToken);
			Target = await node.AppendAsync(data, options, writer, cancellationToken);
		}

		/// <summary>
		/// Appends data to this file
		/// </summary>
		/// <param name="stream">Data to append to the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task AppendAsync(Stream stream, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken)
		{
			FileNode node = await ExpandAsync(cancellationToken);
			Target = await node.AppendAsync(stream, options, writer, cancellationToken);
		}

		/// <inheritdoc/>
		public override string ToString() => Name.ToString();
	}

	/// <summary>
	/// Entry for a directory within a directory node
	/// </summary>
	public class DirectoryEntry : TreeNodeRef<DirectoryNode>
	{
		/// <summary>
		/// Name of this directory
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Length of this directory tree
		/// </summary>
		public long Length => (Target == null) ? _cachedLength : Target.Length;

		/// <summary>
		/// Cached value for the length of this tree
		/// </summary>
		readonly long _cachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(Utf8String name, DirectoryNode node)
			: base(node)
		{
			Name = name;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="reader"></param>
		public DirectoryEntry(ITreeNodeReader reader)
			: base(reader)
		{
			Name = reader.ReadUtf8String();
			_cachedLength = (long)reader.ReadUnsignedVarInt();
		}

		/// <summary>
		/// Serialize this directory entry to disk
		/// </summary>
		/// <param name="writer"></param>
		public new void Serialize(ITreeNodeWriter writer)
		{
			base.Serialize(writer);

			writer.WriteUtf8String(Name);
			writer.WriteUnsignedVarInt((ulong)Length);
		}

		/// <inheritdoc/>
		public override string ToString() => Name.ToString();
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
	public class DirectoryNode : TreeNode
	{
		internal const byte TypeId = (byte)'d';

		readonly Dictionary<Utf8String, FileEntry> _nameToFileEntry = new Dictionary<Utf8String, FileEntry>();
		readonly Dictionary<Utf8String, DirectoryEntry> _nameToDirectoryEntry = new Dictionary<Utf8String, DirectoryEntry>();

		/// <summary>
		/// Total size of this directory
		/// </summary>
		public long Length => Files.Sum(x => x.Length) + Directories.Sum(x => x.Length);

		/// <summary>
		/// Flags for this directory 
		/// </summary>
		public DirectoryFlags Flags { get; }

		/// <summary>
		/// All the files within this directory
		/// </summary>
		public IReadOnlyCollection<FileEntry> Files => _nameToFileEntry.Values;

		/// <summary>
		/// All the subdirectories within this directory
		/// </summary>
		public IReadOnlyCollection<DirectoryEntry> Directories => _nameToDirectoryEntry.Values;

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
			byte typeId = reader.ReadUInt8();
			if (typeId != TypeId)
			{
				throw new InvalidOperationException("Invalid signature byte for directory");
			}

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
			writer.WriteUInt8(TypeId);
			writer.WriteUnsignedVarInt((ulong)Flags);

			writer.WriteUnsignedVarInt(Files.Count);
			foreach (FileEntry fileEntry in Files)
			{
				fileEntry.Serialize(writer);
			}

			writer.WriteUnsignedVarInt(Directories.Count);
			foreach (DirectoryEntry directoryEntry in Directories)
			{
				directoryEntry.Serialize(writer);
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
			if (TryGetDirectoryEntry(entry.Name, out _))
			{
				throw new ArgumentException($"A directory with the name {entry.Name} already exists");
			}

			_nameToFileEntry.Add(entry.Name, entry);
			MarkAsDirty();
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="flags">Flags for the new file</param>
		/// <returns>The new directory object</returns>
		public FileEntry AddFile(Utf8String name, FileEntryFlags flags)
		{
			FileEntry entry = new FileEntry(name, flags);
			AddFile(entry);
			return entry;
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="stream">The stream to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async Task<FileEntry> AddFileAsync(Utf8String name, FileEntryFlags flags, Stream stream, ChunkingOptions options, TreeWriter writer, CancellationToken cancellationToken = default)
		{
			FileEntry entry = AddFile(name, flags);
			await entry.AppendAsync(stream, options, writer, cancellationToken);
			return entry;
		}

		/// <summary>
		/// Finds or adds a file with the given path
		/// </summary>
		/// <param name="path">Path to the file</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<FileEntry> AddFileByPathAsync(Utf8String path, FileEntryFlags flags, CancellationToken cancellationToken = default)
		{
			DirectoryNode directory = this;

			Utf8String remainingPath = path;
			if (remainingPath[0] == '/' || remainingPath[0] == '\\')
			{
				remainingPath = remainingPath.Substring(1);
			}

			for (; ; )
			{
				int length = 0;
				for (; ; length++)
				{
					if (length == remainingPath.Length)
					{
						return directory.AddFile(remainingPath, flags);
					}

					byte character = remainingPath[length];
					if (character == '\\' || character == '/')
					{
						break;
					}
				}

				if (length > 0)
				{
					directory = await directory.FindOrAddDirectoryAsync(remainingPath.Slice(0, length), cancellationToken);
				}
				remainingPath = remainingPath.Slice(length + 1);
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
		/// <param name="path"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async ValueTask<bool> DeleteFileByPathAsync(Utf8String path, CancellationToken cancellationToken)
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
					directory = await directory.FindDirectoryAsync(remainingPath.Slice(0, length), cancellationToken);
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
		/// <param name="name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public DirectoryNode AddDirectory(Utf8String name)
		{
			if (TryGetFileEntry(name, out _))
			{
				throw new ArgumentException($"A file with the name '{name}' already exists in this directory", nameof(name));
			}

			DirectoryNode node = new DirectoryNode(Flags);

			DirectoryEntry entry = new DirectoryEntry(name, node);
			_nameToDirectoryEntry.Add(name, entry);
			MarkAsDirty();

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
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode?> FindDirectoryAsync(Utf8String name, CancellationToken cancellationToken)
		{
			if (TryGetDirectoryEntry(name, out DirectoryEntry? entry))
			{
				return await entry.ExpandAsync(cancellationToken);
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode> FindOrAddDirectoryAsync(Utf8String name, CancellationToken cancellationToken)
		{
			DirectoryNode? directory = await FindDirectoryAsync(name, cancellationToken);
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
		/// Adds files from a directory on disk
		/// </summary>
		/// <param name="directoryInfo"></param>
		/// <param name="options">Options for chunking file content</param>
		/// <param name="writer">Writer for new node data</param>
		/// <param name="logger"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyFromDirectoryAsync(DirectoryInfo directoryInfo, ChunkingOptions options, TreeWriter writer, ILogger logger, CancellationToken cancellationToken)
		{
			const int MaxWriters = 32;
			const long MinSizePerWriter = 1024 * 1024;

			// Enumerate all the files below this directory
			List<(DirectoryNode DirectoryNode, FileInfo FileInfo)> files = new List<(DirectoryNode, FileInfo)>();
			FindFilesToCopy(directoryInfo, files);

			// Compute the total size
			long totalSize = files.Sum(x => x.Item2.Length);
			long chunkSize = Math.Max(MinSizePerWriter, totalSize / MaxWriters);

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
				tasks.Add(Task.Run(() => CopyFilesAsync(files, minIdxCopy, maxIdx, fileEntries, options, writer, logger, cancellationToken), cancellationToken));

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

		static async Task CopyFilesAsync(List<(DirectoryNode DirectoryNode, FileInfo FileInfo)> files, int minIdx, int maxIdx, FileEntry[] entries, ChunkingOptions options, TreeWriter baseWriter, ILogger logger, CancellationToken cancellationToken)
		{
			TreeWriter writer = new TreeWriter(baseWriter);
			for(int idx = minIdx; idx < maxIdx; idx++)
			{
				FileInfo fileInfo = files[idx].FileInfo;

				FileEntry fileEntry = new FileEntry(fileInfo.Name, FileEntryFlags.None);
				using (Stream stream = fileInfo.OpenRead())
				{
					await fileEntry.AppendAsync(stream, options, writer, cancellationToken);
				}
				entries[idx] = fileEntry;
			}
			await writer.FlushAsync(cancellationToken);
		}

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryInfo"></param>
		/// <param name="logger"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task CopyToDirectoryAsync(DirectoryInfo directoryInfo, ILogger logger, CancellationToken cancellationToken)
		{
			directoryInfo.Create();

			List<Task> tasks = new List<Task>();
			foreach (FileEntry fileEntry in Files)
			{
				FileInfo fileInfo = new FileInfo(Path.Combine(directoryInfo.FullName, fileEntry.Name.ToString()));
				FileNode fileNode = await fileEntry.ExpandAsync(cancellationToken);
				logger.LogInformation("Writing {File}", fileInfo.FullName);
				tasks.Add(Task.Run(() => fileNode.CopyToFileAsync(fileInfo, cancellationToken), cancellationToken));
			}
			foreach (DirectoryEntry directoryEntry in Directories)
			{
				DirectoryInfo subDirectoryInfo = directoryInfo.CreateSubdirectory(directoryEntry.Name.ToString());
				DirectoryNode subDirectoryNode = await directoryEntry.ExpandAsync(cancellationToken);
				logger.LogInformation("Writing {Dir}", subDirectoryInfo.FullName);
				tasks.Add(Task.Run(() => subDirectoryNode.CopyToDirectoryAsync(subDirectoryInfo, logger, cancellationToken), cancellationToken));
			}

			await Task.WhenAll(tasks);
		}
	}

	/// <summary>
	/// Factory for creating and serializing <see cref="DirectoryNode"/> objects
	/// </summary>
	public class DirectoryNodeSerializer : TreeNodeSerializer<DirectoryNode>
	{
		/// <inheritdoc/>
		public override DirectoryNode Deserialize(ITreeNodeReader reader) => new DirectoryNode(reader);
	}

	/// <summary>
	/// Extension methods for <see cref="DirectoryNode"/>
	/// </summary>
	public static class DirectoryNodeExtensions
	{
	}
}
