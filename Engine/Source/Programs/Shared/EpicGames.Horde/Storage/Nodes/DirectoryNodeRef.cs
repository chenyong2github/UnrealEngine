// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage.Nodes
{
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
}
