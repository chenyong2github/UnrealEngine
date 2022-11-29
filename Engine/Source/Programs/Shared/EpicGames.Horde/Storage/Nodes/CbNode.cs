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
		public IReadOnlyDictionary<IoHash, TreeNodeRef> References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="obj">The compact binary object</param>
		/// <param name="references">Map of attachment hash to node locator</param>
		public CbNode(CbObject obj, IReadOnlyDictionary<IoHash, NodeLocator> references)
		{
			Object = obj;
			References = references.ToDictionary(x => x.Key, x => new TreeNodeRef(new RefTarget(x.Key, x.Value)));
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public CbNode(ITreeNodeReader reader)
		{
			Object = new CbObject(reader.ReadFixedLengthBytes(reader.Length));
			References = reader.References.ToDictionary(x => x.Key, x => new TreeNodeRef(new RefTarget(x.Key, x.Value)));
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteFixedLengthBytes(Object.GetView().Span);
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs() => References.Values;
	}
}
