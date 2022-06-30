// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Flags for a directory entry
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
		/// The attached entry includes a Git SHA1 of the corresponding blob/tree contents.
		/// </summary>
		HasGitSha1 = 32,

		/// <summary>
		/// The data for this entry is a Perforce depot path and revision rather than the actual file contents.
		/// </summary>
		PerforceDepotPathAndRevision = 64,
	}

	/// <summary>
	/// Entry for a file within a directory node
	/// </summary>
	public class FileEntry : TreeNodeRef<FileNode>
	{
		/// <summary>
		/// Name of this file
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Flags for this file
		/// </summary>
		public FileEntryFlags Flags { get; }

		/// <summary>
		/// Length of this entry
		/// </summary>
		public long Length => (Node == null) ? _cachedLength : Node.Length;

		/// <summary>
		/// Cached length of this node
		/// </summary>
		long _cachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(DirectoryNode owner, Utf8String name, FileEntryFlags flags, FileNode node)
			: base(owner, node)
		{
			Name = name;
			Flags = flags;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(DirectoryNode owner, Utf8String name, FileEntryFlags flags, long length, ITreeBlob target)
			: base(owner, target)
		{
			Name = name;
			Flags = flags;
			
			_cachedLength = length;
		}

		/// <inheritdoc/>
		protected override void OnCollapse()
		{
			_cachedLength = Node!.Length;
		}

		/// <inheritdoc/>
		public override string ToString() => IsDirty() ? $"{Name} (*)" : Name.ToString();
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
		public long Length => (Node == null) ? _cachedLength : Node.Length;

		/// <summary>
		/// Cached value for the length of this tree
		/// </summary>
		long _cachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(DirectoryNode owner, Utf8String name, DirectoryNode node)
			: base(owner, node)
		{
			Name = name;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(DirectoryNode owner, Utf8String name, long length, ITreeBlob target)
			: base(owner, target)
		{
			Name = name;

			_cachedLength = length;
		}

		/// <inheritdoc/>
		protected override void OnCollapse()
		{
			_cachedLength = Node!.Length;
		}

		/// <inheritdoc/>
		public override string ToString() => IsDirty() ? $"{Name} (*)" : Name.ToString();
	}

	/// <summary>
	/// A directory node
	/// </summary>
	[TreeSerializer(typeof(DirectoryNodeDeserializer))]
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
		/// All the files within this directory
		/// </summary>
		public IReadOnlyCollection<FileEntry> Files => _nameToFileEntry.Values;

		/// <summary>
		/// All the subdirectories within this directory
		/// </summary>
		public IReadOnlyCollection<DirectoryEntry> Directories => _nameToDirectoryEntry.Values;

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

		/// <inheritdoc/>
		public override IReadOnlyList<TreeNodeRef> GetReferences()
		{
			List<TreeNodeRef> refs = new List<TreeNodeRef>();
			refs.AddRange(Files);
			refs.AddRange(Directories);
			return refs;
		}

		#region File operations

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="data">Data for the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async Task<FileNode> AddFileAsync(Utf8String name, FileEntryFlags flags, ReadOnlyMemory<byte> data, ChunkingOptions options, CancellationToken cancellationToken)
		{
			using ReadOnlyMemoryStream stream = new ReadOnlyMemoryStream(data);
			return await AddFileAsync(name, flags, stream, options, cancellationToken);
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="name">Name of the new directory</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="stream">The stream to read from</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async Task<FileNode> AddFileAsync(Utf8String name, FileEntryFlags flags, Stream stream, ChunkingOptions options, CancellationToken cancellationToken)
		{
			if(TryGetDirectoryEntry(name, out _))
			{
				throw new ArgumentException($"A directory with the name {name} already exists");
			}

			FileNode newNode = await FileNode.CreateAsync(stream, options, cancellationToken);

			FileEntry entry = new FileEntry(this, name, flags, newNode);
			_nameToFileEntry[name] = entry;
			MarkAsDirty();

			return newNode;
		}

		/// <summary>
		/// Finds or adds a file with the given path
		/// </summary>
		/// <param name="path">Path to the file</param>
		/// <param name="flags">Flags for the new file</param>
		/// <param name="stream">Stream to add to the file</param>
		/// <param name="options">Options for chunking the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<FileNode> AddFileByPathAsync(Utf8String path, FileEntryFlags flags, Stream stream, ChunkingOptions options, CancellationToken cancellationToken)
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
						return await directory.AddFileAsync(remainingPath, flags, stream, options, cancellationToken);
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
			for (DirectoryNode? directory = this; directory != null; )
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

			DirectoryNode node = new DirectoryNode();

			DirectoryEntry entry = new DirectoryEntry(this, name, node);
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
			if(_nameToDirectoryEntry.Remove(name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		#endregion

		/// <inheritdoc/>
		public async ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CancellationToken cancellationToken)
		{
			List<ITreeBlob> childNodes = new List<ITreeBlob>();

			List<FileEntry> fileEntries = _nameToFileEntry.Values.OrderBy(x => x.Name).ToList();
			foreach (FileEntry fileEntry in fileEntries)
			{
				childNodes.Add(await fileEntry.CollapseAsync(writer, cancellationToken));
			}

			List<DirectoryEntry> directoryEntries = _nameToDirectoryEntry.Values.OrderBy(x => x.Name).ToList();
			foreach (DirectoryEntry directoryEntry in directoryEntries)
			{
				childNodes.Add(await directoryEntry.CollapseAsync(writer, cancellationToken));
			}

			ReadOnlySequence<byte> data = SerializeData(fileEntries, directoryEntries);
			return await writer.WriteBlobAsync(data, childNodes, cancellationToken);
		}

		static ReadOnlySequence<byte> SerializeData(List<FileEntry> fileEntries, List<DirectoryEntry> directoryEntries)
		{
			// Measure the required size of the write buffer
			int size = 1;
			size += VarInt.MeasureUnsigned(fileEntries.Count);
			foreach (FileEntry fileEntry in fileEntries)
			{
				size += (fileEntry.Name.Length + 1) + 1 + VarInt.MeasureUnsigned((ulong)fileEntry.Length);
			}
			size += VarInt.MeasureUnsigned(directoryEntries.Count);
			foreach (DirectoryEntry directoryEntry in directoryEntries)
			{
				size += (directoryEntry.Name.Length + 1) + VarInt.MeasureUnsigned((ulong)directoryEntry.Length);
			}

			// Allocate the buffer and copy the node to it
			byte[] data = new byte[size];
			Span<byte> span = data.AsSpan();

			span[0] = TypeId;
			span = span.Slice(1);

			int fileCountBytes = VarInt.WriteUnsigned(span, fileEntries.Count);
			span = span.Slice(fileCountBytes);

			foreach (FileEntry fileEntry in fileEntries)
			{
				fileEntry.Name.Span.CopyTo(span);
				span = span.Slice(fileEntry.Name.Length + 1);

				span[0] = (byte)fileEntry.Flags;
				span = span.Slice(1);

				int lengthBytes = VarInt.WriteUnsigned(span, fileEntry.Length);
				span = span.Slice(lengthBytes);
			}

			int directoryCountBytes = VarInt.WriteUnsigned(span, directoryEntries.Count);
			span = span.Slice(directoryCountBytes);

			foreach (DirectoryEntry directoryEntry in directoryEntries)
			{
				directoryEntry.Name.Span.CopyTo(span);
				span = span.Slice(directoryEntry.Name.Length + 1);

				int lengthBytes = VarInt.WriteUnsigned(span, directoryEntry.Length);
				span = span.Slice(lengthBytes);
			}

			Debug.Assert(span.Length == 0);
			return new ReadOnlySequence<byte>(data);
		}

		/// <summary>
		/// Deserialize a directory node from data
		/// </summary>
		/// <param name="payload">The data to read from</param>
		/// <param name="children">Child nodes</param>
		/// <returns>The deserialized directory node</returns>
		public static DirectoryNode Deserialize(ReadOnlySequence<byte> payload, IReadOnlyList<ITreeBlob> children)
		{
			ReadOnlySpan<byte> span = payload.AsSingleSegment().Span;
			if(span[0] != TypeId)
			{
				throw new InvalidOperationException("Invalid signature byte for directory");
			}

			span = span.Slice(1);

			DirectoryNode node = new DirectoryNode();

			int fileCount = (int)VarInt.ReadUnsigned(span, out int fileCountBytes);
			span = span.Slice(fileCountBytes);

			int childIdx = 0;

			node._nameToFileEntry.EnsureCapacity(fileCount);
			for (int idx = 0; idx < fileCount; idx++)
			{
				int nameLen = span.IndexOf((byte)0);
				Utf8String name = new Utf8String(span.Slice(0, nameLen).ToArray());
				span = span.Slice(nameLen + 1);

				FileEntryFlags flags = (FileEntryFlags)span[0];
				span = span.Slice(1);

				long length = (long)VarInt.ReadUnsigned(span, out int lengthBytes);
				span = span.Slice(lengthBytes);

				node._nameToFileEntry[name] = new FileEntry(node, name, flags, length, children[childIdx++]);
			}

			int directoryCount = (int)VarInt.ReadUnsigned(span, out int directoryCountBytes);
			span = span.Slice(directoryCountBytes);

			node._nameToDirectoryEntry.EnsureCapacity(directoryCount);
			for (int idx = 0; idx < directoryCount; idx++)
			{
				int nameLen = span.IndexOf((byte)0);
				Utf8String name = new Utf8String(span.Slice(0, nameLen).ToArray());
				span = span.Slice(nameLen + 1);

				long length = (long)VarInt.ReadUnsigned(span, out int lengthBytes);
				span = span.Slice(lengthBytes);

				node._nameToDirectoryEntry[name] = new DirectoryEntry(node, name, length, children[childIdx++]);
			}

			Debug.Assert(span.Length == 0);
			return node;
		}

		/// <summary>
		/// Adds files from a directory on disk
		/// </summary>
		/// <param name="directoryInfo"></param>
		/// <param name="options">Options for chunking file content</param>
		/// <param name="logger"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyFromDirectoryAsync(DirectoryInfo directoryInfo, ChunkingOptions options, ILogger logger, CancellationToken cancellationToken)
		{
			foreach (DirectoryInfo subDirectoryInfo in directoryInfo.EnumerateDirectories())
			{
				logger.LogInformation("Adding {Directory}", subDirectoryInfo.FullName);
				DirectoryNode subDirectoryNode = AddDirectory(subDirectoryInfo.Name);
				await subDirectoryNode.CopyFromDirectoryAsync(subDirectoryInfo, options, logger, cancellationToken);
			}
			foreach (FileInfo fileInfo in directoryInfo.EnumerateFiles())
			{
				logger.LogInformation("Adding {File}", fileInfo.FullName);
				using Stream stream = fileInfo.OpenRead();
				FileNode fileNode = await AddFileAsync(fileInfo.Name, 0, stream, options, cancellationToken);
			}
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
				tasks.Add(Task.Run(() => fileNode.CopyToFileAsync(fileInfo, cancellationToken), cancellationToken));
			}
			foreach (DirectoryEntry directoryEntry in Directories)
			{
				DirectoryInfo subDirectoryInfo = directoryInfo.CreateSubdirectory(directoryEntry.Name.ToString());
				DirectoryNode subDirectoryNode = await directoryEntry.ExpandAsync(cancellationToken);
				tasks.Add(Task.Run(() => subDirectoryNode.CopyToDirectoryAsync(subDirectoryInfo, logger, cancellationToken), cancellationToken));
			}

			await Task.WhenAll(tasks);
		}
	}

	/// <summary>
	/// Factory for creating and serializing <see cref="DirectoryNode"/> objects
	/// </summary>
	public class DirectoryNodeDeserializer : TreeNodeSerializer<DirectoryNode>
	{
		/// <inheritdoc/>
		public override async ValueTask<DirectoryNode> DeserializeAsync(ITreeBlob blob, CancellationToken cancellationToken) => DirectoryNode.Deserialize(await blob.GetDataAsync(cancellationToken), await blob.GetReferencesAsync(cancellationToken));

		/// <inheritdoc/>
		public override ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, DirectoryNode node, CancellationToken cancellationToken) => node.SerializeAsync(writer, cancellationToken);
	}

	/// <summary>
	/// Extension methods for <see cref="DirectoryNode"/>
	/// </summary>
	public static class DirectoryNodeExtensions
	{
	}
}
