// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles.Nodes
{
	/// <summary>
	/// Flags for a directory entry
	/// </summary>
	public enum FileEntryFlags : byte
	{
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
	public class FileEntry : BundleNodeRef<FileNode>
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
		public long Length => (Node == null) ? CachedLength : Node.Length;

		/// <summary>
		/// Cached length of this node
		/// </summary>
		long CachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(Utf8String Name, FileEntryFlags Flags, FileNode Node)
			: base(Node)
		{
			this.Name = Name;
			this.Flags = Flags;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public FileEntry(Bundle Bundle, Utf8String Name, FileEntryFlags Flags, long Length, IoHash Hash)
			: base(Bundle, Hash)
		{
			this.Name = Name;
			this.Flags = Flags;
			this.CachedLength = Length;
		}

		/// <inheritdoc/>
		protected override void Collapse()
		{
			CachedLength = Node!.Length;
		}
	}

	/// <summary>
	/// Entry for a directory within a directory node
	/// </summary>
	public class DirectoryEntry : BundleNodeRef<DirectoryNode>
	{
		/// <summary>
		/// Name of this directory
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Length of this directory tree
		/// </summary>
		public long Length => (Node == null) ? CachedLength : Node.Length;

		/// <summary>
		/// Cached value for the length of this tree
		/// </summary>
		long CachedLength;

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(Utf8String Name, DirectoryNode Node)
			: base(Node)
		{
			this.Name = Name;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public DirectoryEntry(Bundle Bundle, Utf8String Name, long Length, IoHash Hash)
			: base(Bundle, Hash)
		{
			this.Name = Name;
			this.CachedLength = Length;
		}

		/// <inheritdoc/>
		protected override void Collapse()
		{
			CachedLength = Node!.Length;
		}
	}

	/// <summary>
	/// A directory node
	/// </summary>
	[BundleNodeDeserializer(typeof(DirectoryNodeDeserializer))]
	public class DirectoryNode : BundleNode
	{
		internal const byte TypeId = (byte)'d';

		Dictionary<Utf8String, FileEntry> NameToFileEntry = new Dictionary<Utf8String, FileEntry>();
		Dictionary<Utf8String, DirectoryEntry> NameToDirectoryEntry = new Dictionary<Utf8String, DirectoryEntry>();

		/// <summary>
		/// Total size of this directory
		/// </summary>
		public long Length => Files.Sum(x => x.Length) + Directories.Sum(x => x.Length);

		/// <summary>
		/// All the files within this directory
		/// </summary>
		public IReadOnlyCollection<FileEntry> Files => NameToFileEntry.Values;

		/// <summary>
		/// All the subdirectories within this directory
		/// </summary>
		public IReadOnlyCollection<DirectoryEntry> Directories => NameToDirectoryEntry.Values;

		/// <summary>
		/// Clear the contents of this directory
		/// </summary>
		public void Clear()
		{
			NameToFileEntry.Clear();
			NameToDirectoryEntry.Clear();
			MarkAsDirty();
		}

		/// <summary>
		/// Check whether an entry with the given name exists in this directory
		/// </summary>
		/// <param name="Name">Name of the entry to search for</param>
		/// <returns>True if the entry exists</returns>
		public bool Contains(Utf8String Name) => TryGetFileEntry(Name, out _) || TryGetDirectoryEntry(Name, out _);

		/// <inheritdoc/>
		public override IEnumerable<BundleNodeRef> GetReferences() => Enumerable.Concat<BundleNodeRef>(Files, Directories);

		#region File operations

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <param name="Flags">Flags for the new file</param>
		/// <returns>The new directory object</returns>
		public FileNode CreateFile(Utf8String Name, FileEntryFlags Flags)
		{
			if(TryGetDirectoryEntry(Name, out _))
			{
				throw new ArgumentException($"A directory with the name {Name} already exists");
			}

			FileNode NewNode = new FileNode();

			FileEntry Entry = new FileEntry(Name, Flags, NewNode);
			NameToFileEntry[Name] = Entry;
			MarkAsDirty();

			return NewNode;
		}

		/// <summary>
		/// Finds or adds a file with the given path
		/// </summary>
		/// <param name="Path">Path to the file</param>
		/// <param name="Flags">Flags for the new file</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<FileNode> CreateFileByPathAsync(Utf8String Path, FileEntryFlags Flags)
		{
			DirectoryNode Directory = this;

			Utf8String RemainingPath = Path;
			for (; ; )
			{
				int Length = RemainingPath.IndexOf('/');
				if (Length == -1)
				{
					return Directory.CreateFile(RemainingPath, Flags);
				}
				if (Length > 0)
				{
					Directory = await Directory.FindOrAddDirectoryAsync(RemainingPath.Slice(0, Length));
				}
				RemainingPath = RemainingPath.Slice(Length + 1);
			}
		}

		/// <summary>
		/// Attempts to get a file entry with the given name
		/// </summary>
		/// <param name="Name">Name of the file</param>
		/// <param name="Entry">Entry for the file</param>
		/// <returns>True if the file was found</returns>
		public bool TryGetFileEntry(Utf8String Name, [NotNullWhen(true)] out FileEntry? Entry) => NameToFileEntry.TryGetValue(Name, out Entry);

		/// <summary>
		/// Deletes the file entry with the given name
		/// </summary>
		/// <param name="Name">Name of the entry to delete</param>
		/// <returns>True if the entry was found, false otherwise</returns>
		public bool DeleteFile(Utf8String Name)
		{
			if (NameToFileEntry.Remove(Name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Path"></param>
		/// <returns></returns>
		public async ValueTask<bool> DeleteFileByPathAsync(Utf8String Path)
		{
			DirectoryNode Directory = this;

			Utf8String RemainingPath = Path;
			for (; ; )
			{
				int Length = RemainingPath.IndexOf('/');
				if (Length == -1)
				{
					return Directory.DeleteFile(RemainingPath);
				}
				if (Length > 0)
				{
					Directory = await Directory.FindOrAddDirectoryAsync(RemainingPath.Slice(0, Length));
				}
				RemainingPath = RemainingPath.Slice(Length + 1);
			}
		}

		#endregion

		#region Directory operations

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public DirectoryNode AddDirectory(Utf8String Name)
		{
			if (TryGetFileEntry(Name, out _))
			{
				throw new ArgumentException($"A file with the name '{Name}' already exists in this directory", nameof(Name));
			}

			DirectoryNode Node = new DirectoryNode();

			DirectoryEntry Entry = new DirectoryEntry(Name, Node);
			NameToDirectoryEntry.Add(Name, Entry);
			MarkAsDirty();

			return Node;
		}

		/// <summary>
		/// Attempts to get a directory entry with the given name
		/// </summary>
		/// <param name="Name">Name of the directory</param>
		/// <param name="Entry">Entry for the directory</param>
		/// <returns>True if the directory was found</returns>
		public bool TryGetDirectoryEntry(Utf8String Name, [NotNullWhen(true)] out DirectoryEntry? Entry) => NameToDirectoryEntry.TryGetValue(Name, out Entry);

		/// <summary>
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode?> FindDirectoryAsync(Utf8String Name)
		{
			if (TryGetDirectoryEntry(Name, out DirectoryEntry? Entry))
			{
				return await Entry.GetAsync();
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Tries to get a directory with the given name, or adds it if not present
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode> FindOrAddDirectoryAsync(Utf8String Name)
		{
			DirectoryNode? Node = await FindDirectoryAsync(Name);
			return Node ?? AddDirectory(Name);
		}

		/// <summary>
		/// Deletes the file entry with the given name
		/// </summary>
		/// <param name="Name">Name of the entry to delete</param>
		/// <returns>True if the entry was found, false otherwise</returns>
		public bool DeleteDirectory(Utf8String Name)
		{
			if(NameToDirectoryEntry.Remove(Name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		#endregion

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> Serialize()
		{
			// Measure the required size of the write buffer
			int Size = 1;
			Size += VarInt.Measure(NameToFileEntry.Count);
			foreach (FileEntry Entry in NameToFileEntry.Values)
			{
				Size += (Entry.Name.Length + 1) + 1 + VarInt.Measure((ulong)Entry.Length) + IoHash.NumBytes;
			}
			Size += VarInt.Measure(NameToDirectoryEntry.Count);
			foreach (DirectoryEntry Entry in NameToDirectoryEntry.Values)
			{
				Size += (Entry.Name.Length + 1) + VarInt.Measure((ulong)Entry.Length) + IoHash.NumBytes;
			}

			// Allocate the buffer and copy the node to it
			byte[] Data = new byte[Size];
			Span<byte> Span = Data.AsSpan();

			Span[0] = TypeId;
			Span = Span.Slice(1);

			int FileCountBytes = VarInt.Write(Span, NameToFileEntry.Count);
			Span = Span.Slice(FileCountBytes);

			foreach (FileEntry FileEntry in NameToFileEntry.Values.OrderBy(x => x.Name))
			{
				FileEntry.Name.Span.CopyTo(Span);
				Span = Span.Slice(FileEntry.Name.Length + 1);

				Span[0] = (byte)FileEntry.Flags;
				Span = Span.Slice(1);

				int LengthBytes = VarInt.Write(Span, FileEntry.Length);
				Span = Span.Slice(LengthBytes);

				FileEntry.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);
			}

			int DirectoryCountBytes = VarInt.Write(Span, NameToDirectoryEntry.Count);
			Span = Span.Slice(DirectoryCountBytes);

			foreach (DirectoryEntry DirectoryEntry in NameToDirectoryEntry.Values.OrderBy(x => x.Name))
			{
				DirectoryEntry.Name.Span.CopyTo(Span);
				Span = Span.Slice(DirectoryEntry.Name.Length + 1);

				int LengthBytes = VarInt.Write(Span, DirectoryEntry.Length);
				Span = Span.Slice(LengthBytes);

				DirectoryEntry.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);
			}

			Debug.Assert(Span.Length == 0);
			return Data;
		}

		internal static DirectoryNode Deserialize(Bundle Bundle, ReadOnlySpan<byte> Span)
		{
			if(Span[0] != TypeId)
			{
				throw new InvalidOperationException("Invalid signature byte for directory");
			}

			Span = Span.Slice(1);

			DirectoryNode Node = new DirectoryNode();

			int FileCount = (int)VarInt.Read(Span, out int FileCountBytes);
			Span = Span.Slice(FileCountBytes);

			Node.NameToFileEntry.EnsureCapacity(FileCount);
			for (int Idx = 0; Idx < FileCount; Idx++)
			{
				int NameLen = Span.IndexOf((byte)0);
				Utf8String Name = new Utf8String(Span.Slice(0, NameLen).ToArray());
				Span = Span.Slice(NameLen + 1);

				FileEntryFlags Flags = (FileEntryFlags)Span[0];
				Span = Span.Slice(1);

				long Length = (long)VarInt.Read(Span, out int LengthBytes);
				Span = Span.Slice(LengthBytes);

				IoHash Hash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				Node.NameToFileEntry[Name] = new FileEntry(Bundle, Name, Flags, Length, Hash);
			}

			int DirectoryCount = (int)VarInt.Read(Span, out int DirectoryCountBytes);
			Span = Span.Slice(DirectoryCountBytes);

			Node.NameToDirectoryEntry.EnsureCapacity(DirectoryCount);
			for (int Idx = 0; Idx < DirectoryCount; Idx++)
			{
				int NameLen = Span.IndexOf((byte)0);
				Utf8String Name = new Utf8String(Span.Slice(0, NameLen).ToArray());
				Span = Span.Slice(NameLen + 1);

				long Length = (long)VarInt.Read(Span, out int LengthBytes);
				Span = Span.Slice(LengthBytes);

				IoHash Hash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				Node.NameToDirectoryEntry[Name] = new DirectoryEntry(Bundle, Name, Length, Hash);
			}

			return Node;
		}

		/// <summary>
		/// Adds files from a directory on disk
		/// </summary>
		/// <param name="DirectoryInfo"></param>
		/// <param name="Options">Options for chunking file content</param>
		/// <param name="Logger"></param>
		/// <returns></returns>
		public async Task CopyFromDirectoryAsync(DirectoryInfo DirectoryInfo, ChunkingOptions Options, ILogger Logger)
		{
			foreach (DirectoryInfo SubDirectoryInfo in DirectoryInfo.EnumerateDirectories())
			{
				Logger.LogInformation("Adding {Directory}", SubDirectoryInfo.FullName);
				DirectoryNode SubDirectoryNode = AddDirectory(SubDirectoryInfo.Name);
				await SubDirectoryNode.CopyFromDirectoryAsync(SubDirectoryInfo, Options, Logger);
			}
			foreach (FileInfo FileInfo in DirectoryInfo.EnumerateFiles())
			{
				Logger.LogInformation("Adding {File}", FileInfo.FullName);
				FileNode FileNode = CreateFile(FileInfo.Name, 0);
 				await FileNode.CopyFromFileAsync(FileInfo, Options);
			}
		}

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="DirectoryInfo"></param>
		/// <param name="Logger"></param>
		/// <returns></returns>
		public async Task CopyToDirectoryAsync(DirectoryInfo DirectoryInfo, ILogger Logger)
		{
			DirectoryInfo.Create();

			List<Task> Tasks = new List<Task>();
			foreach (FileEntry FileEntry in Files)
			{
				FileInfo FileInfo = new FileInfo(Path.Combine(DirectoryInfo.FullName, FileEntry.Name.ToString()));
				FileNode FileNode = await FileEntry.GetAsync();
				Tasks.Add(Task.Run(() => FileNode.CopyToFileAsync(FileInfo)));
			}
			foreach (DirectoryEntry DirectoryEntry in Directories)
			{
				DirectoryInfo SubDirectoryInfo = DirectoryInfo.CreateSubdirectory(DirectoryEntry.Name.ToString());
				DirectoryNode SubDirectoryNode = await DirectoryEntry.GetAsync();
				Tasks.Add(Task.Run(() => SubDirectoryNode.CopyToDirectoryAsync(SubDirectoryInfo, Logger)));
			}

			await Task.WhenAll(Tasks);
		}
	}

	/// <summary>
	/// Factory for creating and serializing <see cref="DirectoryNode"/> objects
	/// </summary>
	public class DirectoryNodeDeserializer : BundleNodeDeserializer<DirectoryNode>
	{
		/// <inheritdoc/>
		public override DirectoryNode Deserialize(Bundle Bundle, ReadOnlyMemory<byte> Data)
		{
			return DirectoryNode.Deserialize(Bundle, Data.Span);
		}
	}
}
