// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
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
	public enum DirectoryEntryFlags : byte
	{
		/// <summary>
		/// This item is a directory
		/// </summary>
		Directory = 1,

		/// <summary>
		/// This item is a file
		/// </summary>
		File = 2,

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
	/// Entry within a directory
	/// </summary>
	public class DirectoryEntry
	{
		/// <summary>
		/// Directory that owns this entry
		/// </summary>
		public DirectoryNode Directory { get; }

		/// <summary>
		/// Name of the entry
		/// </summary>
		public Utf8String Name { get; }

		/// <summary>
		/// Flags for this entry
		/// </summary>
		public DirectoryEntryFlags Flags { get; }

		/// <summary>
		/// Hash of this entry
		/// </summary>
		IoHash Hash;

		/// <summary>
		/// Cached logical representation of this object
		/// </summary>
		BundleNode? Node;

		/// <summary>
		/// Constructor
		/// </summary>
		internal DirectoryEntry(DirectoryNode Directory, Utf8String Name, DirectoryEntryFlags Flags, IoHash Hash)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.Flags = Flags;
			this.Hash = Hash;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		internal DirectoryEntry(DirectoryNode Directory, Utf8String Name, DirectoryEntryFlags Flags, BundleNode Node)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.Flags = Flags;
			this.Node = Node;
		}

		internal IoHash Serialize()
		{
			if (Node != null)
			{
				Hash = Node.Serialize();
			}
			return Hash;
		}

		/// <summary>
		/// Gets the file associated with this entry
		/// </summary>
		/// <returns>File object associated with this entry</returns>
		public async ValueTask<FileNode> GetFileAsync()
		{
			if ((Flags & DirectoryEntryFlags.File) == 0)
			{
				throw new InvalidCastException();
			}
			if (Node == null)
			{
				ReadOnlyMemory<byte> Data = await Directory.Owner.GetDataAsync(Hash);
				Node = new FileNode(Directory.Owner, Directory, Hash, Data);
			}
			return (FileNode)Node;
		}

		/// <summary>
		/// Gets the directory associated with this entry
		/// </summary>
		/// <returns>Directory object associated with this entry</returns>
		public async ValueTask<DirectoryNode> GetDirectoryAsync()
		{
			if ((Flags & DirectoryEntryFlags.Directory) == 0)
			{
				throw new InvalidCastException();
			}
			if (Node == null)
			{
				ReadOnlyMemory<byte> Data = await Directory.Owner.GetDataAsync(Hash);
				Node = DirectoryNode.Deserialize(Directory.Owner, Directory, Hash, Data.Span);
			}
			return (DirectoryNode)Node;
		}
	}

	/// <summary>
	/// A directory node
	/// </summary>
	[BundleNodeFactory(typeof(DirectoryNodeFactory))]
	public class DirectoryNode : BundleNode
	{
		internal const byte TypeId = (byte)'d';

		Dictionary<Utf8String, DirectoryEntry> NameToEntry = new Dictionary<Utf8String, DirectoryEntry>();

		/// <summary>
		/// Entries within this directory
		/// </summary>
		public IReadOnlyCollection<DirectoryEntry> Entries => NameToEntry.Values;

		internal DirectoryNode(Bundle Owner, BundleNode? Parent)
			: base(Owner, Parent)
		{
		}

		internal DirectoryNode(Bundle Owner, BundleNode? Parent, IoHash Hash)
			: base(Owner, Parent, Hash)
		{
		}

		/// <summary>
		/// Clear the contents of this directory
		/// </summary>
		public void Clear()
		{
			NameToEntry.Clear();
			MarkAsDirty();
		}

		/// <summary>
		/// Attempt to get a directory entry with the given name
		/// </summary>
		/// <param name="Name">Name of the entry to find</param>
		/// <param name="Entry">Receives the entry on success, or null on failure</param>
		/// <returns>True if a matching entry was found</returns>
		public bool TryGetEntry(Utf8String Name, [NotNullWhen(true)] out DirectoryEntry? Entry) => NameToEntry.TryGetValue(Name, out Entry);

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <param name="Flags">Flags for the new file</param>
		/// <returns>The new directory object</returns>
		public FileNode CreateFile(Utf8String Name, DirectoryEntryFlags Flags)
		{
			if((Flags & DirectoryEntryFlags.Directory) != 0)
			{
				throw new ArgumentException("Directory flag cannot be specified for new file");
			}

			FileNode NewNode = new FileNode(Owner, this);

			DirectoryEntry Entry = new DirectoryEntry(this, Name, Flags | DirectoryEntryFlags.File, NewNode);
			NameToEntry[Name] = Entry;
			MarkAsDirty();

			return NewNode;
		}

		/// <summary>
		/// Finds or adds a file with the given path
		/// </summary>
		/// <param name="Path">Path to the file</param>
		/// <param name="Flags">Flags for the new file</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<FileNode> CreateFileByPathAsync(Utf8String Path, DirectoryEntryFlags Flags)
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
		/// 
		/// </summary>
		/// <param name="Path"></param>
		/// <returns></returns>
		public async ValueTask DeleteFileByPathAsync(Utf8String Path)
		{
			DirectoryNode Directory = this;

			Utf8String RemainingPath = Path;
			for (; ; )
			{
				int Length = RemainingPath.IndexOf('/');
				if (Length == -1)
				{
					Directory.Delete(RemainingPath);
					return;
				}
				if (Length > 0)
				{
					Directory = await Directory.FindOrAddDirectoryAsync(RemainingPath.Slice(0, Length));
				}
				RemainingPath = RemainingPath.Slice(Length + 1);
			}
		}

		/// <summary>
		/// Adds a new directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public DirectoryNode AddDirectory(Utf8String Name)
		{
			DirectoryNode NewNode = new DirectoryNode(Owner, this);

			DirectoryEntry Entry = new DirectoryEntry(this, Name, DirectoryEntryFlags.Directory, NewNode);
			NameToEntry.Add(Name, Entry);
			MarkAsDirty();

			return NewNode;
		}

		/// <summary>
		/// Tries to get a directory with the given name
		/// </summary>
		/// <param name="Name">Name of the new directory</param>
		/// <returns>The new directory object</returns>
		public async ValueTask<DirectoryNode?> FindDirectoryAsync(Utf8String Name)
		{
			if (TryGetEntry(Name, out DirectoryEntry? Entry))
			{
				return await Entry.GetDirectoryAsync();
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
		public bool Delete(Utf8String Name)
		{
			if(NameToEntry.Remove(Name))
			{
				MarkAsDirty();
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		protected override IoHash SerializeDirty()
		{
			int Size = 1;
			foreach (DirectoryEntry Entry in NameToEntry.Values)
			{
				Size += 1 + (Entry.Name.Length + 1) + IoHash.NumBytes;
			}

			byte[] Data = new byte[Size];
			Span<byte> Span = Data.AsSpan();

			Span[0] = TypeId;
			Span = Span.Slice(1);

			List<IoHash> References = new List<IoHash>();
			foreach (DirectoryEntry Entry in NameToEntry.Values.OrderBy(x => x.Name))
			{
				IoHash EntryHash = Entry.Serialize();
				References.Add(EntryHash);

				Span[0] = (byte)Entry.Flags;
				Span = Span.Slice(1);

				Entry.Name.Span.CopyTo(Span);
				Span = Span.Slice(Entry.Name.Length + 1);

				EntryHash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);
			}

			return Owner.WriteNode(Data, References);
		}

		internal static DirectoryNode Deserialize(Bundle Owner, BundleNode? Parent, IoHash Hash, ReadOnlySpan<byte> Span)
		{
			if(Span[0] != TypeId)
			{
				throw new InvalidOperationException("Invalid signature byte for directory");
			}

			Span = Span.Slice(1);

			DirectoryNode Node = new DirectoryNode(Owner, Parent);
			while (Span.Length > 0)
			{
				DirectoryEntryFlags Flags = (DirectoryEntryFlags)Span[0];
				Span = Span.Slice(1);

				int Length = Span.IndexOf((byte)0);
				Utf8String Name = new Utf8String(Span.Slice(0, Length).ToArray());
				Span = Span.Slice(Length + 1);

				IoHash EntryHash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				Node.NameToEntry[Name] = new DirectoryEntry(Node, Name, Flags, EntryHash);
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
				FileNode FileNode = CreateFile(FileInfo.Name, DirectoryEntryFlags.File);
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
			foreach (DirectoryEntry Entry in Entries)
			{
				if ((Entry.Flags & DirectoryEntryFlags.Directory) != 0)
				{
					DirectoryInfo SubDirectoryInfo = DirectoryInfo.CreateSubdirectory(Entry.Name.ToString());
					DirectoryNode SubDirectoryNode = await Entry.GetDirectoryAsync();
					Tasks.Add(Task.Run(() => SubDirectoryNode.CopyToDirectoryAsync(SubDirectoryInfo, Logger)));
				}
				else
				{
					FileInfo FileInfo = new FileInfo(Path.Combine(DirectoryInfo.FullName, Entry.Name.ToString()));
					FileNode FileNode = await Entry.GetFileAsync();
					Tasks.Add(Task.Run(() => FileNode.CopyToFileAsync(FileInfo)));
				}
			}

			await Task.WhenAll(Tasks);
		}
	}

	/// <summary>
	/// Factory for creating and serializing <see cref="DirectoryNode"/> objects
	/// </summary>
	public class DirectoryNodeFactory : BundleNodeFactory<DirectoryNode>
	{
		/// <inheritdoc/>
		public override DirectoryNode CreateRoot(Bundle Bundle)
		{
			return new DirectoryNode(Bundle, null);
		}

		/// <inheritdoc/>
		public override DirectoryNode ParseRoot(Bundle Bundle, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			return DirectoryNode.Deserialize(Bundle, null, Hash, Data.Span);
		}
	}
}
