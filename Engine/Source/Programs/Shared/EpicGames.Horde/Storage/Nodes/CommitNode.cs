// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// A node representing commit metadata
	/// </summary>
	[TreeNode("{64D50724-6B22-41C0-A890-B51CD6241817}", 1)]
	public class CommitNode : TreeNode
	{
		/// <summary>
		/// The commit number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Reference to the parent commit
		/// </summary>
		public TreeNodeRef<CommitNode>? Parent { get; set; }

		/// <summary>
		/// Message for this commit
		/// </summary>
		public string Message { get; set; }

		/// <summary>
		/// Time that this commit was created
		/// </summary>
		public DateTime Time { get; set; }

		/// <summary>
		/// Contents of the tree at this commit
		/// </summary>
		public TreeNodeRef<DirectoryNode> Contents { get; set; }

		/// <summary>
		/// Metadata for this commit, keyed by arbitrary GUID
		/// </summary>
		public Dictionary<Guid, TreeNodeRef> Metadata { get; } = new Dictionary<Guid, TreeNodeRef>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="number">Commit number</param>
		/// <param name="parent">The parent commit</param>
		/// <param name="message">Message for the commit</param>
		/// <param name="time">The commit time</param>
		/// <param name="contents">Contents of the tree at this commit</param>
		public CommitNode(int number, TreeNodeRef<CommitNode>? parent, string message, DateTime time, TreeNodeRef<DirectoryNode> contents)
		{
			Number = number;
			Parent = parent;
			Message = message;
			Time = time;
			Contents = contents;
		}

		/// <summary>
		/// Deserializing constructor
		/// </summary>
		/// <param name="reader"></param>
		public CommitNode(ITreeNodeReader reader)
		{
			Number = (int)reader.ReadUnsignedVarInt();
			if (reader.ReadBoolean())
			{
				Parent = reader.ReadRef<CommitNode>();
			}
			Message = reader.ReadString();
			Time = reader.ReadDateTime();
			Contents = reader.ReadRef<DirectoryNode>();
			Metadata = reader.ReadDictionary(() => reader.ReadGuid(), () => reader.ReadRef());
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteUnsignedVarInt(Number);
			writer.WriteBoolean(Parent != null);
			if (Parent != null)
			{
				writer.WriteRef(Parent);
			}
			writer.WriteString(Message);
			writer.WriteDateTime(Time);
			writer.WriteRef(Contents);
			writer.WriteDictionary(Metadata, key => writer.WriteGuid(key), value => writer.WriteRef(value));
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs()
		{
			if (Parent != null)
			{
				yield return Parent;
			}
			yield return Contents;
		}
	}
}
