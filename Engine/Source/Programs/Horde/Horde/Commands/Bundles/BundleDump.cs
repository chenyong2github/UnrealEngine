// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;

namespace Horde.Commands.Bundles
{
	[Command("bundle", "dump", "Dumps the contents of a bundle")]
	internal class BundleDump : Command
	{
		[CommandLine("-Input=")]
		public FileReference Input { get; set; } = null!;

		[CommandLine("-Verbose")]
		public bool Verbose { get; set; }

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			byte[] bytes = await FileReference.ReadAllBytesAsync(Input);
			Bundle bundle = new Bundle(new MemoryReader(bytes));
			logger.LogInformation("Summary for blob {Location}", Input);

			BundleHeader header = bundle.Header;

			Dictionary<Guid, string> typeIdToName = new Dictionary<Guid, string>();

			TreeReaderOptions options = new TreeReaderOptions();
			foreach (Type type in options.Types)
			{
				TreeNodeAttribute? attribute = type.GetCustomAttribute<TreeNodeAttribute>();
				if (attribute != null)
				{
					typeIdToName.Add(Guid.Parse(attribute.Guid), type.Name);
				}
			}

			string[] types = new string[header.Types.Count];
			for (int idx = 0; idx < header.Types.Count; idx++)
			{
				string? name;
				if (!typeIdToName.TryGetValue(header.Types[idx].Guid, out name))
				{
					name = header.Types[idx].Guid.ToString();
				}
				types[idx] = $"{name} v{header.Types[idx].Version}";
			}

			int packetStart = 0;

			logger.LogInformation("");
			logger.LogInformation("Imports: {NumImports}", header.Imports.Count);

			List<string> nodeNames = new List<string>();

			int refIdx = 0;
			foreach (BundleImport import in header.Imports)
			{
				logger.LogInformation("  From blob {BlobId} ({NumExports} nodes)", import.Locator, import.Exports.Count);
				foreach (int exportIdx in import.Exports)
				{
					nodeNames.Add($"IMP {import.Locator}#{exportIdx}");
					if (Verbose)
					{
						logger.LogInformation("    [{Index}] IMP {ExportIdx}", refIdx, exportIdx);
					}
					refIdx++;
				}
			}

			logger.LogInformation("");
			logger.LogInformation("Exports: {NumExports}", header.Exports.Count);

			int packetIdx = 0;
			int packetOffset = 0;
			for(int exportIdx = 0; exportIdx < header.Exports.Count; exportIdx++)
			{
				BundleExport export = header.Exports[exportIdx];
				logger.LogInformation("  [{Index}] EXP {ExportHash} (type: {Type}, length: {NumBytes:n0}, packet: {PacketIdx})", refIdx, export.Hash, types[export.TypeIdx], export.Length, packetIdx);

				nodeNames.Add($"EXP {exportIdx}");
				refIdx++;

				if (Verbose)
				{
					for (int idx = 0; idx < export.References.Count; idx++)
					{
						int nodeIdx = export.References[idx];
						logger.LogInformation("            REF {RefIdx,-3} -> {NodeIdx,-3} ({RefName})", idx, nodeIdx, nodeNames[nodeIdx]);
					}
				}

				packetOffset += export.Length;
				if(packetOffset >= header.Packets[packetIdx].DecodedLength)
				{
					packetIdx++;
					packetOffset = 0;
				}
			}

			logger.LogInformation("");
			logger.LogInformation("Packets: {NumPackets}", header.Packets.Count);
			for (int idx = 0; idx < header.Packets.Count; idx++)
			{
				BundlePacket packet = header.Packets[idx];
				logger.LogInformation("  PKT {Idx} (file offset: {Offset:n0}, encoded: {EncodedLength:n0}, decoded: {DecodedLength:n0}, ratio: {Ratio}%)", idx, packetStart, packet.EncodedLength, packet.DecodedLength, (int)(packet.EncodedLength * 100) / packet.DecodedLength);
				packetStart += packet.EncodedLength;
			}

			return 0;
		}
	}
}
