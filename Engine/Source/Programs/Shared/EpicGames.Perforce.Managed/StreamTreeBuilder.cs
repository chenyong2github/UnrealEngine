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
		public StreamTreeRef Encode(Dictionary<IoHash, CbObject> Objects)
		{
			StreamTreeRef? Ref = EncodeInternal(Objects);
			if (Ref == null)
			{
				CbObject Object = new StreamTree().ToCbObject(Utf8String.Empty);
				IoHash ObjectHash = Object.GetHash();
				Objects[ObjectHash] = Object;
				Ref = new StreamTreeRef(Utf8String.Empty, ObjectHash);
			}
			return Ref;
		}

		/// <summary>
		/// Encodes the current tree state and returns a reference to it
		/// </summary>
		/// <param name="Objects">Dictionary of encoded objects</param>
		/// <returns></returns>
		public StreamTreeRef? EncodeInternal(Dictionary<IoHash, CbObject> Objects)
		{
			// Recursively serialize all the child items
			foreach ((Utf8String SubTreeName, StreamTreeBuilder SubTree) in NameToTreeBuilder)
			{
				StreamTreeRef? SubTreeRef = SubTree.EncodeInternal(Objects);
				if (SubTreeRef == null)
				{
					NameToTreeBuilder.Remove(SubTreeName);
				}
				else
				{
					NameToTree[SubTreeName] = SubTreeRef;
				}
			}
			NameToTreeBuilder.Clear();

			// If if's empty, don't create this tree at all
			if (IsEmpty)
			{
				return null;
			}

			// Find the common base path for all items in this tree.
			StreamTree Tree = new StreamTree(NameToFile, NameToTree);
			Utf8String BasePath = Tree.FindBasePath();
			CbObject PackedObject = Tree.ToCbObject(BasePath);

			IoHash ObjectHash = PackedObject.GetHash();
			Objects[ObjectHash] = PackedObject;

			return new StreamTreeRef(BasePath, ObjectHash);
		}
	}
}
