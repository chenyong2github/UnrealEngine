// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Helper class for writing/updating directory trees within a tree pack
	/// </summary>
	public class TreePackDirWriter
	{
		TreePack TreePack;
		Dictionary<Utf8String, TreePackDirEntry> NameToEntry = new Dictionary<Utf8String, TreePackDirEntry>();
		Dictionary<Utf8String, TreePackDirWriter> NameToSubDir = new Dictionary<Utf8String, TreePackDirWriter>();

		public IEnumerable<TreePackDirEntry> Entries => NameToEntry.Values;

		/// <summary>
		/// Constructor. Creates an empty, new directory structure.
		/// </summary>
		/// <param name="TreePack"></param>
		public TreePackDirWriter(TreePack TreePack)
		{
			this.TreePack = TreePack;
		}

		/// <summary>
		/// Cosntructor. Initializes the contents of the tree to the root node in the given ref.
		/// </summary>
		/// <param name="TreePack"></param>
		/// <param name="Ref"></param>
		public TreePackDirWriter(TreePack TreePack, IRef Ref)
			: this(TreePack, TreePackDirNode.Parse(TreePack.AddRootObject(Ref).GetRootNode()))
		{
		}

		/// <summary>
		/// Constructor. Initializes the contents of the tree to an existing directory node.
		/// </summary>
		/// <param name="TreePack"></param>
		/// <param name="Node"></param>
		public TreePackDirWriter(TreePack TreePack, TreePackDirNode Node)
		{
			this.TreePack = TreePack;

			foreach (TreePackDirEntry Entry in Node.Entries)
			{
				NameToEntry[Entry.Name] = Entry;
			}
		}

		/// <summary>
		/// Clears the current directory. 
		/// </summary>
		public void Clear()
		{
			NameToEntry.Clear();
			NameToSubDir.Clear();
		}

		/// <summary>
		/// Adds a file to the tree pack with the given path. 
		/// </summary>
		/// <param name="Path">Path to the file. Should be specified using forward slashes as directory separators, and without a leading slash.</param>
		/// <param name="Flags">Flags for the new file entry.</param>
		/// <param name="Hash">Hash of the file.</param>
		/// <param name="GitSha1">SHA1 of the file (optional)</param>
		/// <returns>New directory entry</returns>
		public async ValueTask<TreePackDirEntry> FindOrAddFileByPathAsync(Utf8String Path, TreePackDirEntryFlags Flags, IoHash Hash, Sha1Hash GitSha1 = default)
		{
			TreePackDirWriter Writer = this;
			for (; ; )
			{
				int Index = Path.IndexOf('/');
				if (Index != -1)
				{
					Writer = await Writer.FindOrAddDirAsync(Path.Substring(0, Index));
					Path = Path.Substring(Index + 1);
					continue;
				}
				return Writer.FindOrAddFile(Path, Flags, Hash, GitSha1);
			}
		}

		/// <summary>
		/// Adds a file to the current tree level with the given name. 
		/// </summary>
		/// <param name="Name">Name of the file. Should not contain any path information.</param>
		/// <param name="Flags">Flags for the new file entry.</param>
		/// <param name="Hash">Hash of the file.</param>
		/// <param name="GitSha1">SHA1 of the file (optional)</param>
		/// <returns>New directory entry</returns>
		public TreePackDirEntry FindOrAddFile(Utf8String Name, TreePackDirEntryFlags Flags, IoHash Hash, Sha1Hash GitSha1 = default)
		{
			TreePackDirEntry Entry = new TreePackDirEntry(Flags, Name, Hash, GitSha1);
			NameToEntry[Name] = Entry;
			NameToSubDir.Remove(Name);
			return Entry;
		}

		/// <summary>
		/// Gets a directory writer to the current tree level for the given directory name
		/// </summary>
		/// <param name="Name"></param>
		/// <returns>Writer for the given directory name. Null if a directory by this name does not exist.</returns>
		public async ValueTask<TreePackDirWriter?> FindDirAsync(Utf8String Name)
		{
			TreePackDirWriter? Writer;
			if (!NameToSubDir.TryGetValue(Name, out Writer))
			{
				TreePackDirEntry? Entry;
				if (NameToEntry.TryGetValue(Name, out Entry) && (Entry.Flags & TreePackDirEntryFlags.Directory) != 0)
				{
					Writer = await FromDirNodeAsync(TreePack, Entry.Hash);
					NameToEntry.Remove(Name);
					NameToSubDir.Add(Name, Writer);
				}
			}
			return Writer;
		}

		/// <summary>
		/// Adds a directory to the current tree level with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <returns></returns>
		public async ValueTask<TreePackDirWriter> FindOrAddDirAsync(Utf8String Name)
		{
			TreePackDirWriter? Writer = await FindDirAsync(Name);
			if (Writer == null)
			{
				Writer = new TreePackDirWriter(TreePack);
				NameToSubDir.Add(Name, Writer);
			}
			return Writer;
		}

		/// <summary>
		/// Adds a file to the tree pack with the given path. 
		/// </summary>
		/// <param name="Path">Path to the file. Should be specified using forward slashes as directory separators, and without a leading slash.</param>
		public async Task RemoveFileByPathAsync(Utf8String Path)
		{
			int Index = Path.IndexOf('/');
			if (Index == -1)
			{
				if (NameToEntry.TryGetValue(Path, out TreePackDirEntry? Entry) && (Entry.Flags & TreePackDirEntryFlags.File) != 0)
				{
					NameToEntry.Remove(Path);
				}
			}
			else
			{
				Utf8String DirName = Path.Substring(0, Index);

				TreePackDirWriter? SubDirWriter = await FindDirAsync(DirName);
				if (SubDirWriter != null)
				{
					await SubDirWriter.RemoveFileByPathAsync(Path.Substring(Index + 1));
					if (SubDirWriter.NameToEntry.Count == 0 && SubDirWriter.NameToSubDir.Count == 0)
					{
						NameToSubDir.Remove(DirName);
					}
				}
			}
		}

		/// <summary>
		/// Flushes the current tree state and writes it to the tree pack
		/// </summary>
		/// <returns>Hash of the tree</returns>
		public async Task<IoHash> FlushAsync()
		{
			foreach ((Utf8String Name, TreePackDirWriter Writer) in NameToSubDir)
			{
				IoHash Hash = await Writer.FlushAsync();
				NameToEntry[Name] = new TreePackDirEntry(TreePackDirEntryFlags.Directory, Name, Hash, Sha1Hash.Zero);
			}
			NameToSubDir.Clear();

			TreePackDirNode Node = new TreePackDirNode();
			Node.Entries.AddRange(NameToEntry.Values);
			return await TreePack.AddNodeAsync(Node.ToByteArray());
		}

		/// <summary>
		/// Writes the directory tree to the storage client with the 
		/// </summary>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <returns></returns>
		public async Task WriteAsync(BucketId BucketId, RefId RefId)
		{
			IoHash RootHash = await FlushAsync();
			await TreePack.WriteAsync(BucketId, RefId, RootHash, DateTime.UtcNow);
		}

		/// <summary>
		/// Creates a writer from a particular directory hash
		/// </summary>
		/// <param name="TreePack"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<TreePackDirWriter> FromDirNodeAsync(TreePack TreePack, IoHash Hash)
		{
			ReadOnlyMemory<byte> Data = await TreePack.GetDataAsync(Hash);
			TreePackDirNode SubDirNode = TreePackDirNode.Parse(Data);
			return new TreePackDirWriter(TreePack, SubDirNode);
		}
	}
}
