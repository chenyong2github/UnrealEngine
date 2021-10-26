// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Utilities;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Contains a source block of text to be indexed
	/// </summary>
	public class LogIndexBlock
	{
		/// <summary>
		/// First line within the file
		/// </summary>
		public int LineIndex { get; }

		/// <summary>
		/// Number of lines in this block
		/// </summary>
		public int LineCount { get; }

		/// <summary>
		/// The plain text for this block
		/// </summary>
		public ILogText? CachedPlainText { get; private set; }

		/// <summary>
		/// The compressed plain text data
		/// </summary>
		public ReadOnlyMemory<byte> CompressedPlainText { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="LineIndex">Index of the first line within this block</param>
		/// <param name="LineCount">Number of lines in the block</param>
		/// <param name="CachedPlainText">The decompressed plain text</param>
		/// <param name="CompressedPlainText">The compressed text data</param>
		public LogIndexBlock(int LineIndex, int LineCount, ILogText? CachedPlainText, ReadOnlyMemory<byte> CompressedPlainText)
		{
			this.LineIndex = LineIndex;
			this.LineCount = LineCount;
			this.CachedPlainText = CachedPlainText;
			this.CompressedPlainText = CompressedPlainText;
		}

		/// <summary>
		/// Accessor for the decompressed plaintext
		/// </summary>
		public ILogText InflatePlainText()
		{
			if (CachedPlainText == null)
			{
				CachedPlainText = new ReadOnlyLogText(CompressedPlainText.DecompressBzip2());
			}
			return CachedPlainText;
		}
	}

	/// <summary>
	/// Extension methods for <see cref="LogIndexBlock"/>
	/// </summary>
	public static class LogIndexBlockExtensions
	{
		/// <summary>
		/// Deserialize an index block
		/// </summary>
		/// <param name="Reader">Reader to deserialize from</param>
		/// <returns>The new index block</returns>
		public static LogIndexBlock ReadLogIndexBlock(this MemoryReader Reader)
		{
			int LineIndex = Reader.ReadInt32();
			int LineCount = Reader.ReadInt32();
			ReadOnlyMemory<byte> CompressedPlainText = Reader.ReadVariableLengthBytes();
			return new LogIndexBlock(LineIndex, LineCount, null, CompressedPlainText);
		}

		/// <summary>
		/// Serialize an index block
		/// </summary>
		/// <param name="Writer">Writer to serialize to</param>
		/// <param name="Block">The block to serialize</param>
		public static void WriteLogIndexBlock(this MemoryWriter Writer, LogIndexBlock Block)
		{
			Writer.WriteInt32(Block.LineIndex);
			Writer.WriteInt32(Block.LineCount);
			Writer.WriteVariableLengthBytes(Block.CompressedPlainText.Span);
		}

		/// <summary>
		/// Gets the serialized size of a block
		/// </summary>
		/// <param name="Block">The block to serialize</param>
		public static int GetSerializedSize(this LogIndexBlock Block)
		{
			return sizeof(int) + sizeof(int) + (sizeof(int) + Block.CompressedPlainText.Length);
		}
	}

	/// <summary>
	/// Stats for a search
	/// </summary>
	public class LogSearchStats
	{
		/// <summary>
		/// Number of blocks that were scanned
		/// </summary>
		public int NumScannedBlocks { get; set; }

		/// <summary>
		/// Number of bytes that had to be scanned for results
		/// </summary>
		public int NumScannedBytes { get; set; }

		/// <summary>
		/// Number of blocks that were skipped
		/// </summary>
		public int NumSkippedBlocks { get; set; }

		/// <summary>
		/// Number of blocks that had to be decompressed
		/// </summary>
		public int NumDecompressedBlocks { get; set; }

		/// <summary>
		/// Number of blocks that were searched but did not contain the search term
		/// </summary>
		public int NumFalsePositiveBlocks { get; set; }
	}

	/// <summary>
	/// Data for a log chunk
	/// </summary>
	public class LogIndexData
	{
		/// <summary>
		/// Index for tokens into the block list
		/// </summary>
		ReadOnlyTrie? CachedTrie;

		/// <summary>
		/// Number of bits in the index devoted to the block index
		/// </summary>
		int NumBlockBits;

		/// <summary>
		/// List of text blocks
		/// </summary>
		LogIndexBlock[] Blocks;

		/// <summary>
		/// Empty index data
		/// </summary>
		public static LogIndexData Empty { get; } = new LogIndexData(ReadOnlyTrie.Empty, 0, Array.Empty<LogIndexBlock>());

		/// <summary>
		/// Number of lines covered by the index
		/// </summary>
		public int LineCount => (Blocks.Length > 0) ? (Blocks[Blocks.Length - 1].LineIndex + Blocks[Blocks.Length - 1].LineCount) : 0;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CachedTrie">Index into the text blocks</param>
		/// <param name="NumBlockBits">Number of bits devoted to the block index</param>
		/// <param name="Blocks">Bloom filters for this log file</param>
		public LogIndexData(ReadOnlyTrie? CachedTrie, int NumBlockBits, LogIndexBlock[] Blocks)
		{
			this.CachedTrie = CachedTrie;
			this.NumBlockBits = NumBlockBits;
			this.Blocks = Blocks;
		}

		/// <summary>
		/// Updates the blocks in this index relative to a give line index
		/// </summary>
		/// <param name="LineIndex"></param>
		public void SetBaseLineIndex(int LineIndex)
		{
			for(int BlockIdx = 0; BlockIdx < Blocks.Length; BlockIdx++)
			{
				LogIndexBlock Block = Blocks[BlockIdx];
				Blocks[BlockIdx] = new LogIndexBlock(LineIndex, Block.LineCount, Block.CachedPlainText, Block.CompressedPlainText);
				LineIndex += Block.LineCount;
			}
		}

		/// <summary>
		/// Builds a trie for this index
		/// </summary>
		/// <returns>The trie for this data</returns>
		ReadOnlyTrie BuildTrie()
		{
			if(CachedTrie == null)
			{
				ReadOnlyTrieBuilder Builder = new ReadOnlyTrieBuilder();
				for (int BlockIdx = 0; BlockIdx < Blocks.Length; BlockIdx++)
				{
					LogIndexBlock Block = Blocks[BlockIdx];
					LogToken.GetTokens(Block.InflatePlainText().Data.Span, Token => Builder.Add((Token << NumBlockBits) | (uint)BlockIdx));
				}
				CachedTrie = Builder.Build();
			}
			return CachedTrie;
		}

		/// <summary>
		/// Create an index from an array of blocks
		/// </summary>
		/// <param name="Indexes">List of indexes to merge</param>
		/// <returns>Index data</returns>
		public static LogIndexData Merge(IEnumerable<LogIndexData> Indexes)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("LogIndexData.Merge").StartActive();

			// Create the combined block list
			LogIndexBlock[] NewBlocks = Indexes.SelectMany(x => x.Blocks).ToArray();

			// Figure out how many bits to devote to the block size
			int NewNumBlockBits = 0;
			while(NewBlocks.Length > (1 << NewNumBlockBits))
			{
				NewNumBlockBits += 4;
			}

			// Add all the blocks into a combined index
			ReadOnlyTrieBuilder NewTrieBuilder = new ReadOnlyTrieBuilder();

			int BlockCount = 0;
			foreach(LogIndexData Index in Indexes)
			{
				ulong BlockMask = ((1UL << Index.NumBlockBits) - 1);
				foreach (ulong Value in Index.BuildTrie())
				{
					ulong Token = Value >> Index.NumBlockBits;
					int BlockIdx = (int)(Value & BlockMask);

					ulong NewToken = (Token << NewNumBlockBits) | (ulong)(long)(BlockCount + BlockIdx);
					NewTrieBuilder.Add(NewToken);
				}
				BlockCount += Index.Blocks.Length;
			}

			// Construct the new index data
			ReadOnlyTrie NewTrie = NewTrieBuilder.Build();
			return new LogIndexData(NewTrie, NewNumBlockBits, NewBlocks);
		}

		/// <summary>
		/// Search for the given text in the index
		/// </summary>
		/// <param name="FirstLineIndex">First line index to search from</param>
		/// <param name="Text">Text to search for</param>
		/// <param name="Stats">Receives stats for the search</param>
		/// <returns>List of line numbers for the text</returns>
		public IEnumerable<int> Search(int FirstLineIndex, SearchText Text, LogSearchStats Stats)
		{
			int LastBlockCount = 0;
			foreach (int BlockIdx in EnumeratePossibleBlocks(Text.Bytes, FirstLineIndex))
			{
				LogIndexBlock Block = Blocks[BlockIdx];

				Stats.NumScannedBlocks++;
				Stats.NumDecompressedBlocks += (Block.CachedPlainText == null)? 1 : 0;

				Stats.NumSkippedBlocks += BlockIdx - LastBlockCount;
				LastBlockCount = BlockIdx + 1;

				// Decompress the text
				ILogText BlockText = Block.InflatePlainText();

				// Find the initial offset within this block
				int Offset = 0;
				if(FirstLineIndex > Block.LineIndex)
				{
					int LineIndexWithinBlock = FirstLineIndex - Block.LineIndex;
					Offset = BlockText.LineOffsets[LineIndexWithinBlock];
				}

				// Search within this block
				for(; ;)
				{
					// Find the next offset
					int NextOffset = BlockText.Data.Span.FindNextOcurrence(Offset, Text);
					if(NextOffset == -1)
					{
						Stats.NumScannedBytes += BlockText.Length - Offset;
						break;
					}

					// Update the stats
					Stats.NumScannedBytes += NextOffset - Offset;
					Offset = NextOffset;

					// Check it's not another match within the same line
					int LineIndexWithinBlock = BlockText.GetLineIndexForOffset(Offset);
					yield return Block.LineIndex + LineIndexWithinBlock;

					// Move to the next line
					Offset = BlockText.LineOffsets[LineIndexWithinBlock + 1];
				}

				// If the last scanned bytes is zero, we didn't have any matches from this chunk
				if(Offset == 0 && CachedTrie != null)
				{
					Stats.NumFalsePositiveBlocks++;
				}
			}
			Stats.NumSkippedBlocks += Blocks.Length - LastBlockCount;
		}

		/// <summary>
		/// Search for the given text in the index
		/// </summary>
		/// <param name="Text">Text to search for</param>
		/// <param name="LineIndex">The first </param>
		/// <returns>List of line numbers for the text</returns>
		IEnumerable<int> EnumeratePossibleBlocks(ReadOnlyMemory<byte> Text, int LineIndex)
		{
			// Find the starting block index
			int BlockIdx = Blocks.BinarySearch(x => x.LineIndex, LineIndex);
			if(BlockIdx < 0)
			{
				BlockIdx = Math.Max(~BlockIdx - 1, 0);
			}

			// Make sure we're not starting out of range
			if (BlockIdx < Blocks.Length)
			{
				// In order to be considered a possible block for a positive match, the block must contain an exact list of tokens parsed from the
				// search text, along with a set of partial matches (for substrings that are not guaranteed to start on token boundaries). The former
				// is relatively cheap to compute, and done by stepping through all enumerators for each token in a wave, only returning blocks
				// which all contain the token. At the very least, this must contain blocks in the index from BlockIdx onwards.
				List<IEnumerator<int>> Enumerators = new List<IEnumerator<int>>();
				Enumerators.Add(Enumerable.Range(BlockIdx, Blocks.Length - BlockIdx).GetEnumerator());

				// The second aspect of the search requires a more expensive search through the trie. This is done by running a set of arbitrary 
				// delegates to filter the matches returned from the enumerators.
				List<Predicate<int>> Predicates = new List<Predicate<int>>();

				// If the index has a trie, tokenize the input and generate a list of enumerators and predicates.
				if (CachedTrie != null)
				{
					// Find a list of filters for matching blocks
					HashSet<ulong> Tokens = new HashSet<ulong>();
					for (int TokenPos = 0; TokenPos < Text.Length;)
					{
						ReadOnlySpan<byte> Token = LogToken.GetTokenText(Text.Span, TokenPos);
						if (TokenPos == 0)
						{
							GetUnalignedTokenPredicate(Token, Token.Length == Text.Length, Predicates);
						}
						else
						{
							GetAlignedTokenPredicate(Token, TokenPos + Token.Length == Text.Length, Tokens, Predicates);
						}
						TokenPos += Token.Length;
					}

					// Create an enumerator for each token
					foreach (ulong Token in Tokens)
					{
						ulong MinValue = (Token << NumBlockBits) | (ulong)(long)BlockIdx;
						ulong MaxValue = MinValue + (1UL << NumBlockBits) - 1;

						IEnumerator<int> Enumerator = CachedTrie.EnumerateRange(MinValue, MaxValue).Select(x => (int)(x - MinValue)).GetEnumerator();
						if (!Enumerator.MoveNext())
						{
							yield break;
						}

						Enumerators.Add(Enumerator);
					}
				}

				// Enumerate the matches
				for (; ; )
				{
					// Advance all the enumerators that are behind the current block index. If they are all equal, we have a match.
					bool bMatch = true;
					foreach (IEnumerator<int> Enumerator in Enumerators)
					{
						while (Enumerator.Current < BlockIdx)
						{
							if (!Enumerator.MoveNext())
							{
								yield break;
							}
						}

						if (Enumerator.Current > BlockIdx)
						{
							BlockIdx = Enumerator.Current;
							bMatch = false;
						}
					}

					// Return the match and move to the next block
					if (bMatch)
					{
						if (Predicates.All(Predicate => Predicate(BlockIdx)))
						{
							yield return BlockIdx;
						}
						BlockIdx++;
					}
				}
			}
		}

		/// <summary>
		/// Gets predicates for matching a token that starts 
		/// </summary>
		/// <param name="Text">The token text</param>
		/// <param name="bAllowPartialMatch">Whether to allow a partial match of the token</param>
		/// <param name="Tokens">Set of aligned tokens that are required</param>
		/// <param name="Predicates">List of predicates for the search</param>
		void GetAlignedTokenPredicate(ReadOnlySpan<byte> Text, bool bAllowPartialMatch, HashSet<ulong> Tokens, List<Predicate<int>> Predicates)
		{
			for (int Offset = 0; Offset < Text.Length; Offset += LogToken.MaxTokenBytes)
			{
				ulong Token = LogToken.GetWindowedTokenValue(Text, Offset);
				if (Offset + LogToken.MaxTokenBytes > Text.Length && bAllowPartialMatch)
				{
					ulong TokenMask = LogToken.GetWindowedTokenMask(Text, Offset, true);
					Predicates.Add(BlockIdx => BlockContainsToken(BlockIdx, Token, TokenMask));
					break;
				}
				Tokens.Add(Token);
			}
		}

		/// <summary>
		/// Generates a predicate for matching a token which may or may not start on a regular token boundary
		/// </summary>
		/// <param name="Text">The token text</param>
		/// <param name="bAllowPartialMatch">Whether to allow a partial match of the token</param>
		/// <param name="Predicates">List of predicates for the search</param>
		void GetUnalignedTokenPredicate(ReadOnlySpan<byte> Text, bool bAllowPartialMatch, List<Predicate<int>> Predicates)
		{
			byte[] TextCopy = Text.ToArray();

			Lazy<HashSet<int>> Blocks = new Lazy<HashSet<int>>(() =>
			{
				HashSet<int> Union = new HashSet<int>();
				for (int Shift = 0; Shift < LogToken.MaxTokenBytes; Shift++)
				{
					HashSet<int> Blocks = new HashSet<int>(BlocksContainingToken(TextCopy.AsSpan(), -Shift, bAllowPartialMatch));
					for (int Offset = -Shift + LogToken.MaxTokenBytes; Offset < TextCopy.Length && Blocks.Count > 0; Offset += LogToken.MaxTokenBytes)
					{
						Blocks.IntersectWith(BlocksContainingToken(TextCopy.AsSpan(), Offset, bAllowPartialMatch));
					}
					if (Blocks.Count > 0)
					{
						Union.UnionWith(Blocks);
					}
				}
				return Union;
			});

			Predicates.Add(BlockIdx => Blocks.Value.Contains(BlockIdx));
		}

		/// <summary>
		/// Tests whether a block contains a particular token
		/// </summary>
		/// <param name="BlockIdx">Index of the block to search</param>
		/// <param name="Token">The token to test</param>
		/// <param name="TokenMask">Mask of which bits in the token are valid</param>
		/// <returns>True if the given block contains a token</returns>
		bool BlockContainsToken(int BlockIdx, ulong Token, ulong TokenMask)
		{
			Token = (Token << NumBlockBits) | (uint)BlockIdx;
			TokenMask = (TokenMask << NumBlockBits) | ((1UL << NumBlockBits) - 1);
			return CachedTrie!.EnumerateValues((Value, ValueMask) => (Value & TokenMask) == (Token & ValueMask)).Any();
		}

		/// <summary>
		/// Tests whether a block contains a particular token
		/// </summary>
		/// <param name="Text">The token to test</param>
		/// <param name="Offset">Offset of the window into the token to test</param>
		/// <param name="bAllowPartialMatch">Whether to allow a partial match of the token</param>
		/// <returns>True if the given block contains a token</returns>
		IEnumerable<int> BlocksContainingToken(ReadOnlySpan<byte> Text, int Offset, bool bAllowPartialMatch)
		{
			ulong Token = LogToken.GetWindowedTokenValue(Text, Offset) << NumBlockBits;
			ulong TokenMask = LogToken.GetWindowedTokenMask(Text, Offset, bAllowPartialMatch) << NumBlockBits;
			ulong BlockMask = (1UL << NumBlockBits) - 1;
			return CachedTrie!.EnumerateValues((Value, ValueMask) => (Value & TokenMask) == (Token & ValueMask)).Select(x => (int)(x & BlockMask)).Distinct();
		}

		/// <summary>
		/// Deserialize the index from memory
		/// </summary>
		/// <param name="Reader">Reader to deserialize from</param>
		/// <returns>Index data</returns>
		public static LogIndexData Read(MemoryReader Reader)
		{
			int Version = Reader.ReadInt32();
			if (Version == 0)
			{
				return LogIndexData.Empty;
			}
			else if (Version == 1)
			{
				ReadOnlyTrie Index = Reader.ReadTrie();
				int NumBlockBits = Reader.ReadInt32();

				LogIndexBlock[] Blocks = new LogIndexBlock[Reader.ReadInt32()];
				for (int Idx = 0; Idx < Blocks.Length; Idx++)
				{
					int LineIndex = (Idx > 0) ? (Blocks[Idx - 1].LineIndex + Blocks[Idx - 1].LineCount) : 0;
					int LineCount = Reader.ReadInt32();
					ReadOnlyMemory<byte> CompressedPlainText = Reader.ReadVariableLengthBytes();
					Reader.ReadTrie();
					Blocks[Idx] = new LogIndexBlock(LineIndex, LineCount, null, CompressedPlainText);
				}

				return new LogIndexData(Index, NumBlockBits, Blocks);
			}
			else if (Version == 2)
			{
				ReadOnlyTrie Index = Reader.ReadTrie();
				int NumBlockBits = Reader.ReadInt32();

				LogIndexBlock[] Blocks = new LogIndexBlock[Reader.ReadInt32()];
				for (int Idx = 0; Idx < Blocks.Length; Idx++)
				{
					int LineIndex = Reader.ReadInt32();
					int LineCount = Reader.ReadInt32();
					ReadOnlyMemory<byte> CompressedPlainText = Reader.ReadVariableLengthBytes();
					Reader.ReadTrie();
					Blocks[Idx] = new LogIndexBlock(LineIndex, LineCount, null, CompressedPlainText);
				}

				return new LogIndexData(Index, NumBlockBits, Blocks);
			}
			else if (Version == 3)
			{
				ReadOnlyTrie Index = Reader.ReadTrie();
				int NumBlockBits = Reader.ReadInt32();
				LogIndexBlock[] Blocks = Reader.ReadVariableLengthArray(() => Reader.ReadLogIndexBlock());
				for(int Idx = 1; Idx < Blocks.Length; Idx++)
				{
					if(Blocks[Idx].LineIndex == 0)
					{
						int LineIndex = Blocks[Idx - 1].LineIndex + Blocks[Idx - 1].LineCount;
						Blocks[Idx] = new LogIndexBlock(LineIndex, Blocks[Idx].LineCount, Blocks[Idx].CachedPlainText, Blocks[Idx].CompressedPlainText);
					}
				}
				return new LogIndexData(Index, NumBlockBits, Blocks);
			}
			else
			{
				throw new InvalidDataException($"Invalid index version number {Version}");
			}
		}

		/// <summary>
		/// Serialize an index into memory
		/// </summary>
		/// <param name="Writer">Writer to serialize to</param>
		public void Write(MemoryWriter Writer)
		{
			Writer.WriteInt32(3);
			Writer.WriteTrie(BuildTrie());
			Writer.WriteInt32(NumBlockBits);
			Writer.WriteVariableLengthArray(Blocks, x => Writer.WriteLogIndexBlock(x));
		}

		/// <summary>
		/// Deserialize the index from memory
		/// </summary>
		/// <param name="Memory">Memory to deserialize from</param>
		/// <returns>Index data</returns>
		public static LogIndexData FromMemory(ReadOnlyMemory<byte> Memory)
		{
			MemoryReader Reader = new MemoryReader(Memory);
			return Read(Reader);
		}

		/// <summary>
		/// Serializes the index
		/// </summary>
		/// <returns>Index data</returns>
		public byte[] ToByteArray()
		{
			byte[] Buffer = new byte[GetSerializedSize()];

			MemoryWriter Writer = new MemoryWriter(Buffer);
			Write(Writer);
			Writer.CheckOffset(Buffer.Length);

			return Buffer;
		}

		/// <summary>
		/// Gets the serialized size of this index
		/// </summary>
		/// <returns>The serialized size</returns>
		public int GetSerializedSize()
		{
			return sizeof(int) + BuildTrie().GetSerializedSize() + sizeof(int) + (sizeof(int) + Blocks.Sum(x => x.GetSerializedSize()));
		}
	}

	static class LogIndexDataExtensions
	{
		/// <summary>
		/// Deserialize the index from memory
		/// </summary>
		/// <param name="Reader">Reader to deserialize from</param>
		/// <returns>Index data</returns>
		public static LogIndexData ReadLogIndexData(this MemoryReader Reader)
		{
			return LogIndexData.Read(Reader);
		}

		/// <summary>
		/// Serialize an index into memory
		/// </summary>
		/// <param name="Writer">Writer to serialize to</param>
		/// <param name="Index">The index to write</param>
		public static void WriteLogIndexData(this MemoryWriter Writer, LogIndexData Index)
		{
			Index.Write(Writer);
		}
	}
}
