// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Utility class to efficiently track changes to a StreamTree object in memory
	/// </summary>
	public class StreamTreeBuilder
	{
		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamFile> NameToFile { get; }

		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamTreeRef> NameToTree { get; }

		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamTreeBuilder> NameToTreeBuilder { get; } = new Dictionary<Utf8String, StreamTreeBuilder>(FileUtils.PlatformPathComparerUtf8);

		/// <summary>
		/// Tests whether the tree is empty
		/// </summary>
		public bool IsEmpty => NameToFile.Count == 0 && NameToTree.Count == 0 && NameToTreeBuilder.Count == 0;

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamTreeBuilder()
		{
			NameToFile = new Dictionary<Utf8String, StreamFile>(FileUtils.PlatformPathComparerUtf8);
			NameToTree = new Dictionary<Utf8String, StreamTreeRef>(FileUtils.PlatformPathComparerUtf8);
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Tree"></param>
		public StreamTreeBuilder(StreamTree Tree)
		{
			this.NameToFile = new Dictionary<Utf8String, StreamFile>(Tree.NameToFile, FileUtils.PlatformPathComparerUtf8);
			this.NameToTree = new Dictionary<Utf8String, StreamTreeRef>(Tree.NameToTree, FileUtils.PlatformPathComparerUtf8);
		}

		/// <summary>
		/// Encodes the current tree state and returns a reference to it
		/// </summary>
		/// <param name="Objects">Dictionary of encoded objects</param>
		/// <returns></returns>
		public StreamTree Encode(Func<StreamTree, IoHash> WriteTree)
		{
			// Recursively serialize all the child items
			EncodeChildren(WriteTree);

			// Find the common base path for all items in this tree.
			Dictionary<Utf8String, int> BasePathToCount = new Dictionary<Utf8String, int>();
			foreach ((Utf8String Name, StreamFile File) in NameToFile)
			{
				AddBasePath(BasePathToCount, File.Path, Name);
			}
			foreach ((Utf8String Name, StreamTreeRef Tree) in NameToTree)
			{
				AddBasePath(BasePathToCount, Tree.Path, Name);
			}

			// Create the new tree
			Utf8String BasePath = (BasePathToCount.Count == 0) ? Utf8String.Empty : BasePathToCount.MaxBy(x => x.Value).Key;
			return new StreamTree(BasePath, NameToFile, NameToTree);
		}

		/// <summary>
		/// Encodes a StreamTreeRef from this tree
		/// </summary>
		/// <param name="WriteTree"></param>
		/// <returns>The new tree ref</returns>
		public StreamTreeRef EncodeRef(Func<StreamTree, IoHash> WriteTree)
		{
			StreamTree Tree = Encode(WriteTree);
			return new StreamTreeRef(Tree.Path, WriteTree(Tree));
		}

		/// <summary>
		/// Collapses all of the builders underneath this node
		/// </summary>
		/// <param name="WriteTree"></param>
		public void EncodeChildren(Func<StreamTree, IoHash> WriteTree)
		{
			foreach ((Utf8String SubTreeName, StreamTreeBuilder SubTreeBuilder) in NameToTreeBuilder)
			{
				StreamTree SubTree = SubTreeBuilder.Encode(WriteTree);
				if (SubTree.NameToFile.Count > 0 || SubTree.NameToTree.Count > 0)
				{
					IoHash Hash = WriteTree(SubTree);
					NameToTree[SubTreeName] = new StreamTreeRef(SubTree.Path, Hash);
				}
			}
			NameToTreeBuilder.Clear();
		}

		/// <summary>
		/// Adds the base path of the given item to the count of similar items
		/// </summary>
		/// <param name="BasePathToCount"></param>
		/// <param name="Path"></param>
		/// <param name="Name"></param>
		static void AddBasePath(Dictionary<Utf8String, int> BasePathToCount, Utf8String Path, Utf8String Name)
		{
			if (Path.EndsWith(Name) && Path[^(Name.Length + 1)] == '/')
			{
				Utf8String BasePath = Path[..^(Name.Length + 1)];
				BasePathToCount.TryGetValue(BasePath, out int Count);
				BasePathToCount[BasePath] = Count + 1;
			}
		}
	}
}
