// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
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

			public NodeInfo(IoHash hash, int rank, int length, BlobInfo blob)
			{
				Blob = blob;
				Hash = hash;
				Rank = rank;
				Length = length;
			}
		}

		class BlobInfo
		{
			public IoHash Hash { get; }
			public int Size { get; set; }
			public int DataSize { get; set; }
			public List<NodeInfo>? References { get; set; }
			public Dictionary<IoHash, NodeInfo> Nodes { get; } = new Dictionary<IoHash, NodeInfo>();

			public BlobInfo(IoHash hash, int dataSize)
			{
				Hash = hash;
				DataSize = dataSize;
			}
		}

		/// <summary>
		/// Finds the delta between two bundles
		/// </summary>
		/// <param name="storageClient"></param>
		/// <param name="namespaceId"></param>
		/// <param name="bucketId"></param>
		/// <param name="refId1">First ref to compare</param>
		/// <param name="refId2">Second ref to compare</param>
		/// <param name="logger">Logger to write output data</param>
		public static async Task FindDeltaAsync(IStorageClient storageClient, NamespaceId namespaceId, BucketId bucketId, RefId refId1, RefId refId2, ILogger logger)
		{
			Dictionary<IoHash, BlobInfo> blobCache = new Dictionary<IoHash, BlobInfo>();

			logger.LogInformation("Reading internal blobs...");
			HashSet<NodeInfo> nodes1 = new HashSet<NodeInfo>();
			BlobInfo rootBlob1 = await ScanTreeAsync(storageClient, namespaceId, bucketId, refId1, blobCache, nodes1);

			HashSet<NodeInfo> nodes2 = new HashSet<NodeInfo>();
			BlobInfo rootBlob2 = await ScanTreeAsync(storageClient, namespaceId, bucketId, refId2, blobCache, nodes2);

			logger.LogInformation("Reading leaf blobs...");
			foreach (BlobInfo blob in blobCache.Values)
			{
				if (blob.References == null)
				{
					ReadOnlyMemory<byte> data = await storageClient.ReadBlobToMemoryAsync(namespaceId, blob.Hash);
					MountObject(blob, data, null!);
				}
			}

			logger.LogInformation("");

			logger.LogInformation("{Row}", "                                               |            |       Total       |        Used (Ref 1)        |        Used (Ref 2)        ");
			logger.LogInformation("{Row}", " Identifier                                    |    RawSize |    Num       Size |    Num         Size    Pct |    Num         Size    Pct ");
			logger.LogInformation("{Row}", " ----------------------------------------------|------------|-------------------|----------------------------|----------------------------");

			WriteBlobInfo($"Ref1 {refId1.Hash}", rootBlob1, nodes1, nodes2, logger);
			WriteBlobInfo($"Ref2 {refId2.Hash}", rootBlob2, nodes1, nodes2, logger);

			foreach (BlobInfo blob in blobCache.Values)
			{
				WriteBlobInfo($"Blob {blob.Hash}", blob, nodes1, nodes2, logger);
			}

			logger.LogInformation("");

			List<BlobInfo> newBlobs = nodes2.Select(x => x.Blob).Distinct().Except(nodes2.Select(x => x.Blob)).ToList();
			logger.LogInformation("Total blob download size: {NumBytes:n0} bytes", newBlobs.Sum(x => x.Size));
		}

		static void WriteBlobInfo(string name, BlobInfo blob, HashSet<NodeInfo> nodes1, HashSet<NodeInfo> nodes2, ILogger logger)
		{
			int usedCount1 = 0;
			int usedSize1 = 0;

			int usedCount2 = 0;
			int usedSize2 = 0;

			foreach (NodeInfo node in blob.Nodes.Values)
			{
				if (nodes1.Contains(node))
				{
					usedCount1++;
					usedSize1 += node.Length;
				}
				if (nodes2.Contains(node))
				{
					usedCount2++;
					usedSize2 += node.Length;
				}
			}

			string usedPct1 = $"{(usedSize1 * 100) / blob.DataSize}%";
			string usedPct2 = $"{(usedSize2 * 100) / blob.DataSize}%";

			string row = $"{name,42} | {blob.Size,10:n0} | {blob.Nodes.Count,6:n0} {blob.DataSize,10:n0} | {usedCount1,6:n0} {usedSize1,12:n0} {usedPct1,6} | {usedCount2,6:n0} {usedSize2,12:n0} {usedPct2,6}";
			logger.LogInformation(" {Row}", row);
		}

		static async Task<BlobInfo> ScanTreeAsync(IStorageClient storageClient, NamespaceId namespaceId, BucketId bucketId, RefId refId, Dictionary<IoHash, BlobInfo> blobCache, HashSet<NodeInfo> nodes)
		{
			IRef rootRef = await storageClient.GetRefAsync(namespaceId, bucketId, refId);

			BlobInfo rootBlob = new BlobInfo(IoHash.Zero, 0);
			BundleObject rootObject = MountObject(rootBlob, rootRef.Value.GetView(), blobCache);

			NodeInfo rootNode = rootBlob.Nodes[rootObject.Exports[^1].Hash];
			await ScanTreeAsync(storageClient, namespaceId, rootNode, blobCache, nodes);

			return rootBlob;
		}

		static async Task ScanTreeAsync(IStorageClient storageClient, NamespaceId namespaceId, NodeInfo node, Dictionary<IoHash, BlobInfo> blobCache, HashSet<NodeInfo> nodes)
		{
			if (nodes.Add(node) && node.Rank > 0)
			{
				if (node.Export == null)
				{
					ReadOnlyMemory<byte> data = await storageClient.ReadBlobToMemoryAsync(namespaceId, node.Blob.Hash);
					MountObject(node.Blob, data, blobCache);
				}

				Debug.Assert(node.Export != null);

				foreach (int refIdx in node.Export.References)
				{
					NodeInfo refNode = node.Blob.References![refIdx];
					await ScanTreeAsync(storageClient, namespaceId, refNode, blobCache, nodes);
				}
			}
		}

		static BundleObject MountObject(BlobInfo blob, ReadOnlyMemory<byte> blobData, Dictionary<IoHash, BlobInfo> blobCache)
		{
			BundleObject blobObject = CbSerializer.Deserialize<BundleObject>(blobData);

			blob.Size = blobData.Length;
			blob.DataSize = blobObject.Data.Length;
			blob.References = new List<NodeInfo>();

			foreach (BundleExport export in blobObject.Exports)
			{
				NodeInfo? node;
				if (!blob.Nodes.TryGetValue(export.Hash, out node))
				{
					node = new NodeInfo(export.Hash, export.Rank, export.Length, blob);
					blob.Nodes.Add(export.Hash, node);
				}
				blob.References.Add(node);

				node.Export = export;
			}

			foreach (BundleImportObject importObject in blobObject.ImportObjects)
			{
				BlobInfo? importBlob;
				if (!blobCache.TryGetValue(importObject.Object.Hash, out importBlob))
				{
					importBlob = new BlobInfo(importObject.Object.Hash, importObject.TotalCost);
					blobCache[importObject.Object.Hash] = importBlob;
				}
				foreach (BundleImport import in importObject.Imports)
				{
					NodeInfo? node;
					if (!importBlob.Nodes.TryGetValue(import.Hash, out node))
					{
						node = new NodeInfo(import.Hash, import.Rank, import.Length, importBlob);
						importBlob.Nodes.Add(import.Hash, node);
					}
					blob.References.Add(node);
				}
			}

			return blobObject;
		}

		/// <summary>
		/// Creates a YAML summary of the given bundle tree
		/// </summary>
		/// <param name="storageClient"></param>
		/// <param name="namespaceId"></param>
		/// <param name="bucketId"></param>
		/// <param name="refId"></param>
		/// <param name="outputDir"></param>
		public static async Task WriteSummaryAsync(IStorageClient storageClient, NamespaceId namespaceId, BucketId bucketId, RefId refId, DirectoryReference outputDir)
		{
			IRef rootRef = await storageClient.GetRefAsync(namespaceId, bucketId, refId);
			BundleObject rootObject = CbSerializer.Deserialize<BundleObject>(rootRef.Value);

			FileReference rootFile = FileReference.Combine(outputDir, "root.yml");
			WriteSummaryForObject(rootObject, rootFile);

			await WriteSummaryForImportsAsync(storageClient, namespaceId, rootObject.ImportObjects, new HashSet<IoHash>(), outputDir);
		}

		static async Task WriteSummaryForImportsAsync(IStorageClient storageClient, NamespaceId namespaceId, IEnumerable<BundleImportObject> importObjects, HashSet<IoHash> hashes, DirectoryReference outputDir)
		{
			foreach (BundleImportObject importObject in importObjects)
			{
				IoHash hash = importObject.Object.Hash;
				if (hashes.Add(hash))
				{
					BundleObject blobObject = await storageClient.ReadBlobAsync<BundleObject>(namespaceId, hash);

					FileReference outputFile = FileReference.Combine(outputDir, $"{hash}.yml");
					WriteSummaryForObject(blobObject, outputFile);

					await WriteSummaryForImportsAsync(storageClient, namespaceId, blobObject.ImportObjects, hashes, outputDir);
				}
			}
		}

		/// <summary>
		/// Writes a summary of the given bundle object to disk in YAML format
		/// </summary>
		/// <param name="blobObject"></param>
		/// <param name="file"></param>
		public static void WriteSummaryForObject(BundleObject blobObject, FileReference file)
		{
			DirectoryReference.CreateDirectory(file.Directory);

			using StreamWriter writer = new StreamWriter(FileReference.Open(file, FileMode.Create));

			writer.WriteLine("schema: {0}", blobObject.Schema);

			List<IoHash> references = new List<IoHash>(blobObject.Exports.Select(x => x.Hash));
			if (blobObject.ImportObjects.Count > 0)
			{
				writer.WriteLine("import-objects:");
				foreach (BundleImportObject importObject in blobObject.ImportObjects)
				{
					writer.WriteLine($"- blob: \"{importObject.Object.Hash}\"");
					writer.WriteLine($"  imports:");
					foreach (BundleImport import in importObject.Imports)
					{
						writer.WriteLine($"  - hash: \"{import.Hash}\"");
						writer.WriteLine($"    rank: \"{import.Rank}\"");
						writer.WriteLine($"    cost: \"{import.Length}\"");
						writer.WriteLine("");
						references.Add(import.Hash);
					}
					writer.WriteLine();
				}
			}

			if (blobObject.Exports.Count > 0)
			{
				writer.WriteLine("packets:");
				foreach (IGrouping<int, BundleCompressionPacket> group in blobObject.Exports.Select(x => x.Packet).GroupBy(x => x.Offset).OrderBy(x => x.Key))
				{
					BundleCompressionPacket packet = group.First();
					writer.WriteLine($"- offset: {packet.Offset}");
					writer.WriteLine($"  encodedLength: {packet.EncodedLength}");
					writer.WriteLine($"  decodedLength: {packet.DecodedLength}");
					writer.WriteLine($"  compression-ratio: \"{(packet.EncodedLength * 100) / packet.DecodedLength}%\"");
					writer.WriteLine($"  exports: {group.Count()}");
					writer.WriteLine();
				}

				writer.WriteLine("exports:");
				foreach (BundleExport export in blobObject.Exports)
				{
					writer.WriteLine($"- hash: {export.Hash}");
					writer.WriteLine($"  packet-offset: {export.Packet.Offset}");
					writer.WriteLine($"  size: {export.Length}");
					if (export.References.Count > 0)
					{
						writer.WriteLine($"  references:");
						foreach (int reference in export.References)
						{
							IoHash referenceHash = references[reference];
							writer.WriteLine($"  - \"{referenceHash}\"");
						}
					}
					writer.WriteLine();
				}
			}
		}
	}
}
