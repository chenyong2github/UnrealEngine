// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System.Collections.Generic;
using System.Linq;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// A node containing arbitrary compact binary data
	/// </summary>
	[TreeNode("{34A0793F-8364-42F4-8632-98A71C843229}", 1)]
	public class CbNode : TreeNode
	{
		/// <summary>
		/// The compact binary object
		/// </summary>
		public CbObject Object { get; set; }

		/// <summary>
		/// Imported nodes
		/// </summary>
		public Dictionary<IoHash, NodeLocator> References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="obj">The compact binary object</param>
		/// <param name="references">Map of attachment hash to node locator</param>
		public CbNode(CbObject obj, Dictionary<IoHash, NodeLocator> references)
		{
			Object = obj;
			References = references;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public CbNode(ITreeNodeReader reader)
		{
			Object = new CbObject(reader.ReadFixedLengthBytes(reader.Length));
			References = reader.References.ToDictionary(x => x.Hash, x => x.Locator);
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteFixedLengthBytes(Object.GetView().Span);
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs()
		{
			return References.Select(x => new TreeNodeRef(this, x.Key, x.Value));
		}
	}
}
