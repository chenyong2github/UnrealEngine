// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce.Managed;
using EpicGames.Serialization;
using HordeServer.Storage;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Commits.Impl
{
	using NamespaceId = StringId<INamespace>;

	class CommitTree
	{
		class ObjectFormat
		{
			[CbField("p")]
			public Utf8String Path { get; set; }

			[CbField("h")]
			public IoHash Hash { get; set; }

			[CbField("i")]
			public List<CbBinaryAttachment>? IndexBlobs { get; set; }

			[CbField("d")]
			public List<CbBinaryAttachment>? DataBlobs { get; set; }
		}

		public StreamTreeRef Root { get; }
		public List<CbBinaryAttachment> IndexBlobs { get; }
		public List<CbBinaryAttachment> DataBlobs { get; }

		public CommitTree(StreamTreeRef Root)
			: this(Root, new List<CbBinaryAttachment>(), new List<CbBinaryAttachment>())
		{
		}

		public CommitTree(StreamTreeRef Root, List<CbBinaryAttachment> IndexBlobs, List<CbBinaryAttachment> DataBlobs)
		{
			this.Root = Root;
			this.IndexBlobs = IndexBlobs;
			this.DataBlobs = DataBlobs;
		}

		public CbObject Serialize(Utf8String DefaultPath)
		{
			ObjectFormat Format = new ObjectFormat();
			Format.Path = (Root.Path != DefaultPath) ? Root.Path : Utf8String.Empty;
			Format.Hash = Root.Hash;
			Format.IndexBlobs = IndexBlobs;
			Format.DataBlobs = DataBlobs;
			return CbSerializer.Serialize(Format);
		}

		public static CommitTree Deserialize(CbObject Object, Utf8String DefaultPath)
		{
			ObjectFormat Format = CbSerializer.Deserialize<ObjectFormat>(Object.AsField());
			Utf8String Path = Format.Path.IsEmpty ? DefaultPath : Format.Path;
			return new CommitTree(new StreamTreeRef(Path, Format.Hash), Format.IndexBlobs ?? new List<CbBinaryAttachment>(), Format.DataBlobs ?? new List<CbBinaryAttachment>());
		}
	}
}
