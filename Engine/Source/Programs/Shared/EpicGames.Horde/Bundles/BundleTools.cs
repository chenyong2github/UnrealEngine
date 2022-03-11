// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles
{
	/// <summary>
	/// Tools for manipulating bundles
	/// </summary>
	public static class BundleTools
	{
		class NodeInfo
		{
			public IoHash Hash { get; }
			public int Rank { get; }
			public int Length { get; }
			public BlobInfo Blob { get; }

			public BundleExport? Export { get; set; }

			public NodeInfo(IoHash Hash, int Rank, int Length, BlobInfo Blob)
			{
				this.Blob = Blob;
				this.Hash = Hash;
				this.Rank = Rank;
				this.Length = Length;
			}
		}

		class BlobInfo
		{
			public IoHash Hash { get; }
			public int Size { get; set; }
			public int DataSize { get; set; }
			public List<NodeInfo>? References { get; set; }
			public Dictionary<IoHash, NodeInfo> Nodes { get; } = new Dictionary<IoHash, NodeInfo>();

			public BlobInfo(IoHash Hash, int DataSize)
			{
				this.Hash = Hash;
				this.DataSize = DataSize;
			}
		}

		/// <summary>
		/// Finds the delta between two bundles
		/// </summary>
		/// <param name="StorageClient"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="BucketId"></param>
		/// <param name="RefId1">First ref to compare</param>
		/// <param name="RefId2">Second ref to compare</param>
		/// <param name="Logger">Logger to write output data</param>
		public static async Task FindDeltaAsync(IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId1, RefId RefId2, ILogger Logger)
		{
			Dictionary<IoHash, BlobInfo> BlobCache = new Dictionary<IoHash, BlobInfo>();

			Logger.LogInformation("Reading internal blobs...");
			HashSet<NodeInfo> Nodes1 = new HashSet<NodeInfo>();
			BlobInfo RootBlob1 = await ScanTreeAsync(StorageClient, NamespaceId, BucketId, RefId1, BlobCache, Nodes1);

			HashSet<NodeInfo> Nodes2 = new HashSet<NodeInfo>();
			BlobInfo RootBlob2 = await ScanTreeAsync(StorageClient, NamespaceId, BucketId, RefId2, BlobCache, Nodes2);

			Logger.LogInformation("Reading leaf blobs...");
			foreach (BlobInfo Blob in BlobCache.Values)
			{
				if (Blob.References == null)
				{
					ReadOnlyMemory<byte> Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Blob.Hash);
					MountObject(StorageClient, NamespaceId, Blob, Data, null!);
				}
			}

			Logger.LogInformation("");

			Logger.LogInformation("{Row}", "                                               |            |       Total       |        Used (Ref 1)        |        Used (Ref 2)        ");
			Logger.LogInformation("{Row}", " Identifier                                    |    RawSize |    Num       Size |    Num         Size    Pct |    Num         Size    Pct ");
			Logger.LogInformation("{Row}", " ----------------------------------------------|------------|-------------------|----------------------------|----------------------------");

			WriteBlobInfo($"Ref1 {RefId1.Hash}", RootBlob1, Nodes1, Nodes2, Logger);
			WriteBlobInfo($"Ref2 {RefId2.Hash}", RootBlob2, Nodes1, Nodes2, Logger);

			foreach (BlobInfo Blob in BlobCache.Values)
			{
				WriteBlobInfo($"Blob {Blob.Hash}", Blob, Nodes1, Nodes2, Logger);
			}

			Logger.LogInformation("");

			List<BlobInfo> NewBlobs = Nodes2.Select(x => x.Blob).Distinct().Except(Nodes2.Select(x => x.Blob)).ToList();
			Logger.LogInformation("Total blob download size: {NumBytes:n0} bytes", NewBlobs.Sum(x => x.Size));
		}

		static void WriteBlobInfo(string Name, BlobInfo Blob, HashSet<NodeInfo> Nodes1, HashSet<NodeInfo> Nodes2, ILogger Logger)
		{
			int UsedCount1 = 0;
			int UsedSize1 = 0;

			int UsedCount2 = 0;
			int UsedSize2 = 0;

			foreach (NodeInfo Node in Blob.Nodes.Values)
			{
				if (Nodes1.Contains(Node))
				{
					UsedCount1++;
					UsedSize1 += Node.Length;
				}
				if (Nodes2.Contains(Node))
				{
					UsedCount2++;
					UsedSize2 += Node.Length;
				}
			}

			string UsedPct1 = $"{(UsedSize1 * 100) / Blob.DataSize}%";
			string UsedPct2 = $"{(UsedSize2 * 100) / Blob.DataSize}%";

			string Row = $"{Name,42} | {Blob.Size,10:n0} | {Blob.Nodes.Count,6:n0} {Blob.DataSize,10:n0} | {UsedCount1,6:n0} {UsedSize1,12:n0} {UsedPct1,6} | {UsedCount2,6:n0} {UsedSize2,12:n0} {UsedPct2,6}";
			Logger.LogInformation(" {Row}", Row);
		}

		static async Task<BlobInfo> ScanTreeAsync(IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, Dictionary<IoHash, BlobInfo> BlobCache, HashSet<NodeInfo> Nodes)
		{
			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);

			BlobInfo RootBlob = new BlobInfo(IoHash.Zero, 0);
			BundleObject Object = MountObject(StorageClient, NamespaceId, RootBlob, Ref.Value.GetView(), BlobCache);

			NodeInfo RootNode = RootBlob.Nodes[Object.Exports[^1].Hash];
			await ScanTreeAsync(StorageClient, NamespaceId, RootNode, BlobCache, Nodes);

			return RootBlob;
		}

		static async Task ScanTreeAsync(IStorageClient StorageClient, NamespaceId NamespaceId, NodeInfo Node, Dictionary<IoHash, BlobInfo> BlobCache, HashSet<NodeInfo> Nodes)
		{
			if (Nodes.Add(Node) && Node.Rank > 0)
			{
				if (Node.Export == null)
				{
					ReadOnlyMemory<byte> Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Node.Blob.Hash);
					MountObject(StorageClient, NamespaceId, Node.Blob, Data, BlobCache);
				}

				Debug.Assert(Node.Export != null);

				foreach (int RefIdx in Node.Export.References)
				{
					NodeInfo RefNode = Node.Blob.References![RefIdx];
					await ScanTreeAsync(StorageClient, NamespaceId, RefNode, BlobCache, Nodes);
				}
			}
		}

		static BundleObject MountObject(IStorageClient StorageClient, NamespaceId NamespaceId, BlobInfo Blob, ReadOnlyMemory<byte> BlobData, Dictionary<IoHash, BlobInfo> BlobCache)
		{
			BundleObject Object = CbSerializer.Deserialize<BundleObject>(BlobData);

			Blob.Size = BlobData.Length;
			Blob.DataSize = Object.Data.Length;
			Blob.References = new List<NodeInfo>();

			foreach (BundleExport Export in Object.Exports)
			{
				NodeInfo? Node;
				if (!Blob.Nodes.TryGetValue(Export.Hash, out Node))
				{
					Node = new NodeInfo(Export.Hash, Export.Rank, Export.Length, Blob);
					Blob.Nodes.Add(Export.Hash, Node);
				}
				Blob.References.Add(Node);

				Node.Export = Export;
			}

			foreach (BundleImportObject ImportObject in Object.ImportObjects)
			{
				BlobInfo? ImportBlob;
				if (!BlobCache.TryGetValue(ImportObject.Object.Hash, out ImportBlob))
				{
					ImportBlob = new BlobInfo(ImportObject.Object.Hash, ImportObject.TotalCost);
					BlobCache[ImportObject.Object.Hash] = ImportBlob;
				}
				foreach (BundleImport Import in ImportObject.Imports)
				{
					NodeInfo? Node;
					if (!ImportBlob.Nodes.TryGetValue(Import.Hash, out Node))
					{
						Node = new NodeInfo(Import.Hash, Import.Rank, Import.Length, ImportBlob);
						ImportBlob.Nodes.Add(Import.Hash, Node);
					}
					Blob.References.Add(Node);
				}
			}

			return Object;
		}

		/// <summary>
		/// Creates a YAML summary of the given bundle tree
		/// </summary>
		/// <param name="StorageClient"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="BucketId"></param>
		/// <param name="RefId"></param>
		/// <param name="OutputDir"></param>
		public static async Task WriteSummaryAsync(IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, DirectoryReference OutputDir)
		{
			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);
			BundleObject RootObject = CbSerializer.Deserialize<BundleObject>(Ref.Value);

			FileReference RootFile = FileReference.Combine(OutputDir, "root.yml");
			WriteSummaryForObject(RootObject, RootFile);

			await WriteSummaryForImportsAsync(StorageClient, NamespaceId, RootObject.ImportObjects, new HashSet<IoHash>(), OutputDir);
		}

		static async Task WriteSummaryForImportsAsync(IStorageClient StorageClient, NamespaceId NamespaceId, IEnumerable<BundleImportObject> ImportObjects, HashSet<IoHash> Hashes, DirectoryReference OutputDir)
		{
			foreach (BundleImportObject ImportObject in ImportObjects)
			{
				IoHash Hash = ImportObject.Object.Hash;
				if (Hashes.Add(Hash))
				{
					BundleObject Object = await StorageClient.ReadObjectAsync<BundleObject>(NamespaceId, Hash);

					FileReference OutputFile = FileReference.Combine(OutputDir, $"{Hash}.yml");
					WriteSummaryForObject(Object, OutputFile);

					await WriteSummaryForImportsAsync(StorageClient, NamespaceId, Object.ImportObjects, Hashes, OutputDir);
				}
			}
		}

		/// <summary>
		/// Writes a summary of the given bundle object to disk in YAML format
		/// </summary>
		/// <param name="Object"></param>
		/// <param name="File"></param>
		public static void WriteSummaryForObject(BundleObject Object, FileReference File)
		{
			DirectoryReference.CreateDirectory(File.Directory);

			using StreamWriter Writer = new StreamWriter(FileReference.Open(File, FileMode.Create));

			Writer.WriteLine("schema: {0}", Object.Schema);

			List<IoHash> References = new List<IoHash>(Object.Exports.Select(x => x.Hash));
			if (Object.ImportObjects.Count > 0)
			{
				Writer.WriteLine("import-objects:");
				foreach (BundleImportObject ImportObject in Object.ImportObjects)
				{
					Writer.WriteLine($"- blob: \"{ImportObject.Object.Hash}\"");
					Writer.WriteLine($"  imports:");
					foreach (BundleImport Import in ImportObject.Imports)
					{
						Writer.WriteLine($"  - hash: \"{Import.Hash}\"");
						Writer.WriteLine($"    rank: \"{Import.Rank}\"");
						Writer.WriteLine($"    cost: \"{Import.Length}\"");
						Writer.WriteLine("");
						References.Add(Import.Hash);
					}
					Writer.WriteLine();
				}
			}

			if (Object.Exports.Count > 0)
			{
				Writer.WriteLine("packets:");
				foreach (IGrouping<int, BundleCompressionPacket> Group in Object.Exports.Select(x => x.Packet).GroupBy(x => x.Offset).OrderBy(x => x.Key))
				{
					BundleCompressionPacket Packet = Group.First();
					Writer.WriteLine($"- offset: {Packet.Offset}");
					Writer.WriteLine($"  encodedLength: {Packet.EncodedLength}");
					Writer.WriteLine($"  decodedLength: {Packet.DecodedLength}");
					Writer.WriteLine($"  compression-ratio: \"{(Packet.EncodedLength * 100) / Packet.DecodedLength}%\"");
					Writer.WriteLine($"  exports: {Group.Count()}");
					Writer.WriteLine();
				}

				Writer.WriteLine("exports:");
				foreach (BundleExport Export in Object.Exports)
				{
					Writer.WriteLine($"- hash: {Export.Hash}");
					Writer.WriteLine($"  packet-offset: {Export.Packet.Offset}");
					Writer.WriteLine($"  size: {Export.Length}");
					if (Export.References.Length > 0)
					{
						Writer.WriteLine($"  references:");
						foreach (int Reference in Export.References)
						{
							IoHash ReferenceHash = References[Reference];
							Writer.WriteLine($"  - \"{ReferenceHash}\"");
						}
					}
					Writer.WriteLine();
				}
			}
		}
	}
}
