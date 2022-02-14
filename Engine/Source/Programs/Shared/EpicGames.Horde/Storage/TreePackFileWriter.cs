// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Helper class for writing files split along content-aware boundaries
	/// </summary>
	public class TreePackFileWriter
	{
		public TreePack TreePack { get; }
		int MinBlockSize;
		int MaxBlockSize;
		uint WindowHashThreshold;
		TreePackConcatNode Node = new TreePackConcatNode();
		byte[] Buffer = Array.Empty<byte>();
		int BufferLen;
		BuzHash BuzHash = new BuzHash();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TreePack"></param>
		public TreePackFileWriter(TreePack TreePack)
		{
			this.TreePack = TreePack;
			this.MinBlockSize = TreePack.Options.MinChunkSize;
			this.MaxBlockSize = TreePack.Options.MaxChunkSize;
			this.WindowHashThreshold = (uint)((1L << 32) / TreePack.Options.TargetChunkSize);
		}

		/// <summary>
		/// Resets the currently accumulated data
		/// </summary>
		public void Reset()
		{
			Node.Entries.Clear();
		}

		/// <summary>
		/// Writes data to the pack
		/// </summary>
		/// <param name="Data">Data to be written</param>
		/// <param name="Flush">Whether to flush the contents of the buffer after writing the data</param>
		public async Task WriteAsync(ReadOnlyMemory<byte> Data, bool Flush)
		{
			int Length = 0;
			for (; Length < Data.Length; Length++)
			{
				BuzHash.Add(Data.Span[Length]);

				uint WindowHash = BuzHash.Get();

				int BlockSize = BufferLen + Length;
				if ((BlockSize >= MinBlockSize && WindowHash < WindowHashThreshold) || (BlockSize >= MaxBlockSize))
				{
					await WriteCurrentChunkAsync(Data.Slice(0, Length));
					Data = Data.Slice(Length);
					Length = 0;
				}
			}

			if (Data.Length > 0)
			{
				if (BufferLen + Data.Length > Buffer.Length)
				{
					Array.Resize(ref Buffer, BufferLen + Data.Length);
				}

				Data.CopyTo(Buffer.AsMemory(BufferLen));
				BufferLen += Data.Length;
			}

			if (BufferLen > 0 && Flush)
			{
				await WriteCurrentChunkAsync(Data.Slice(0, Length));
			}
		}

		async Task WriteCurrentChunkAsync(ReadOnlyMemory<byte> Data)
		{
			byte[] Chunk = new byte[1 + BufferLen + Data.Length];
			Chunk[0] = (byte)TreePackNodeType.Binary;
			Buffer.AsMemory(0, BufferLen).CopyTo(Chunk.AsMemory(1));
			Data.CopyTo(Chunk.AsMemory(1 + BufferLen));

			IoHash Hash = await TreePack.AddNodeAsync(Chunk);
			Node.Entries.Add(new TreePackConcatEntry(TreePackConcatNodeFlags.Leaf, Hash, Chunk.Length));

			BufferLen = 0;
			BuzHash.Reset();
		}

		/// <summary>
		/// Flush the contents of the writer
		/// </summary>
		public Task FlushAsync()
		{
			return WriteAsync(ReadOnlyMemory<byte>.Empty, true);
		}

		/// <summary>
		/// Finalizes the current data.
		/// </summary>
		/// <returns>Hash of the finalized data</returns>
		public async Task<IoHash> FinalizeAsync()
		{
			await FlushAsync();
			if (Node.Entries.Count == 1)
			{
				return Node.Entries[0].Hash;
			}
			else
			{
				return await TreePack.AddNodeAsync(Node.ToByteArray());
			}
		}
	}
}
