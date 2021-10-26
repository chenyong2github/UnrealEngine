// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using EpicGames.Serialization.Converters;
using HordeServer.Storage;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Information about a blob stored in a blob pack file
	/// </summary>
	class ObjectPackEntry
	{
		public IoHash Hash { get; }
		public int Offset { get; }
		public int Length { get; }
		public IoHash[] Refs { get; }

		public ObjectPackEntry(IoHash Hash, int Offset, int Length, IoHash[] Refs)
		{
			this.Hash = Hash;
			this.Offset = Offset;
			this.Length = Length;
			this.Refs = Refs;
		}
	}

	/// <summary>
	/// Index for a blob pack
	/// </summary>
	[CbConverter(typeof(ObjectPackIndexConverter))]
	class ObjectPackIndex
	{
		public DateTime Time { get; }
		public ObjectPackEntry[] Blobs { get; }
		public IoHash DataHash { get; }
		public int DataSize;

		Dictionary<IoHash, ObjectPackEntry> HashToInfo;

		public ObjectPackIndex(DateTime Time, ObjectPackEntry[] Blobs, IoHash DataHash, int DataSize)
		{
			this.Time = Time;
			this.Blobs = Blobs;
			this.DataHash = DataHash;
			this.DataSize = DataSize;

			HashToInfo = Blobs.ToDictionary(x => x.Hash, x => x);
		}

		public bool Contains(IoHash Hash) => HashToInfo.ContainsKey(Hash);

		public bool TryGetEntry(IoHash Hash, [NotNullWhen(true)] out ObjectPackEntry? BlobInfo) => HashToInfo.TryGetValue(Hash, out BlobInfo);
	}

	/// <summary>
	/// Converter for BlobPackIndex objects
	/// </summary>
	class ObjectPackIndexConverter : CbConverter<ObjectPackIndex>
	{
		class EncodeFormat
		{
			[CbField("time")]
			public DateTime Time { get; set; }

			[CbField("exports")]
			public IoHash[]? Exports { get; set; }

			[CbField("lengths")]
			public int[]? Lengths { get; set; }

			[CbField("refs")]
			public IoHash[][]? Refs { get; set; }

			[CbField("data")]
			public CbBinaryAttachment DataHash { get; set; }

			[CbField("size")]
			public int DataSize { get; set; }
		}

		/// <inheritdoc/>
		public override ObjectPackIndex Read(CbField Field)
		{
			EncodeFormat Format = CbSerializer.Deserialize<EncodeFormat>(Field);

			ObjectPackEntry[] Objects = new ObjectPackEntry[Format.Exports!.Length];

			int Offset = 0;
			for (int Idx = 0; Idx < Format.Exports.Length; Idx++)
			{
				Objects[Idx] = new ObjectPackEntry(Format.Exports[Idx], Offset, Format.Lengths![Idx], Format.Refs![Idx]);
				Offset += Format.Lengths[Idx];
			}

			return new ObjectPackIndex(Format.Time, Objects, Format.DataHash, Format.DataSize);
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, ObjectPackIndex Index)
		{
			Writer.BeginObject();
			WriteInternal(Writer, Index);
			Writer.EndObject();
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, ObjectPackIndex Index)
		{
			Writer.BeginObject(Name);
			WriteInternal(Writer, Index);
			Writer.EndObject();
		}

		static void WriteInternal(CbWriter Writer, ObjectPackIndex Index)
		{
			EncodeFormat Format = new EncodeFormat();
			Format.Time = Index.Time;
			Format.Exports = Index.Blobs.ConvertAll(x => x.Hash).ToArray();
			Format.Lengths = Index.Blobs.ConvertAll(x => x.Length).ToArray();
			Format.Refs = Index.Blobs.ConvertAll(x => x.Refs).ToArray();
			Format.DataHash = Index.DataHash;
			Format.DataSize = Index.DataSize;
			CbSerializer.Serialize(Writer, Format);
		}
	}

	/// <summary>
	/// Helper class to maintain a set of small objects, re-packing blobs according to a heuristic to balance download performance with churn.
	/// </summary>
	class ObjectSet
	{
		readonly IBlobCollection BlobCollection;
		readonly NamespaceId NamespaceId;
		public int MaxPackSize { get; }

		public HashSet<IoHash> RootSet { get; set; } = new HashSet<IoHash>();

		DateTime Time;

		int NextPackSize;
		byte[] NextPackData;
		List<ObjectPackEntry> NextPackEntries = new List<ObjectPackEntry>();
		Dictionary<IoHash, ObjectPackEntry> NextPackHashToEntry = new Dictionary<IoHash, ObjectPackEntry>();

		public List<ObjectPackIndex> PackIndexes { get; } = new List<ObjectPackIndex>();
		List<Task> WriteTasks = new List<Task>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BlobCollection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="MaxPackSize"></param>
		/// <param name="Time">The initial update time; used to determine the age of blobs</param>
		public ObjectSet(IBlobCollection BlobCollection, NamespaceId NamespaceId, int MaxPackSize, DateTime Time)
		{
			this.BlobCollection = BlobCollection;
			this.NamespaceId = NamespaceId;
			this.MaxPackSize = MaxPackSize;

			NextPackData = null!;

			SetTime(Time);
			Reset();
		}

		/// <summary>
		/// Reset the current state of the next blob
		/// </summary>
		void Reset()
		{
			NextPackSize = 0;
			NextPackData = new byte[MaxPackSize];
			NextPackEntries.Clear();
			NextPackHashToEntry.Clear();
		}

		/// <summary>
		/// Reset the current timestamp
		/// </summary>
		/// <param name="Time">The new timestamp</param>
		public void SetTime(DateTime Time)
		{
			this.Time = Time;
		}

		/// <summary>
		/// Copies an existing entry into storage
		/// </summary>
		/// <param name="Hash">Hash of the data</param>
		/// <param name="Data">The data buffer</param>
		/// <param name="Refs">References to other objects</param>
		public void Add(IoHash Hash, ReadOnlySpan<byte> Data, ReadOnlySpan<IoHash> Refs)
		{
			if (!NextPackHashToEntry.ContainsKey(Hash))
			{
				// Create enough space for the new data
				CreateSpace(Data.Length);

				// Copy the data into the buffer
				Data.CopyTo(NextPackData.AsSpan(NextPackSize));

				// Add the blob
				ObjectPackEntry Entry = new ObjectPackEntry(Hash, NextPackSize, Data.Length, Refs.ToArray());
				NextPackHashToEntry.Add(Hash, Entry);
				NextPackEntries.Add(Entry);
				NextPackSize += Data.Length;
			}
		}

		/// <summary>
		/// Adds an item to the packer
		/// </summary>
		/// <param name="Size">Size of the data</param>
		/// <param name="ReadData">Delegate to copy the data into a span</param>
		/// <param name="Refs">References to other objects</param>
		public IoHash Add(int Size, Action<Memory<byte>> ReadData, IoHash[] Refs)
		{
			// Get the last blob and make sure there's enough space in it
			CreateSpace(Size);

			// Copy the data into the new blob
			Memory<byte> Output = NextPackData.AsMemory(NextPackSize, Size);
			ReadData(Output);

			// Update the metadata for it
			IoHash Hash = IoHash.Compute(Output.Span);
			if (!NextPackHashToEntry.ContainsKey(Hash))
			{
				ObjectPackEntry Entry = new ObjectPackEntry(Hash, NextPackSize, Size, Refs);
				NextPackHashToEntry.Add(Hash, Entry);
				NextPackEntries.Add(Entry);
				NextPackSize += Size;
			}
			return Hash;
		}

		/// <summary>
		/// Finds data for an object with the given hash, from the current pack files
		/// </summary>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public async Task<ReadOnlyMemory<byte>> GetObjectDataAsync(IoHash Hash)
		{
			await Task.WhenAll(WriteTasks);

			ObjectPackEntry? Entry;
			if (NextPackHashToEntry.TryGetValue(Hash, out Entry))
			{
				return NextPackData.AsMemory(Entry.Offset, Entry.Length);
			}

			foreach (ObjectPackIndex Pack in PackIndexes)
			{
				if (Pack.TryGetEntry(Hash, out Entry))
				{
					ReadOnlyMemory<byte> PackData = await BlobCollection.ReadBytesAsync(NamespaceId, Pack.DataHash);
					return PackData.Slice(Entry.Offset, Entry.Length);
				}
			}

			return default;
		}

		/// <summary>
		/// Tries to find an entry for the given hash from the current set of pack files
		/// </summary>
		/// <param name="Hash">Hash of the </param>
		/// <param name="Entry"></param>
		/// <returns></returns>
		public bool TryGetEntry(IoHash Hash, [NotNullWhen(true)] out ObjectPackEntry? Entry)
		{
			ObjectPackEntry? LocalEntry;
			if (NextPackHashToEntry.TryGetValue(Hash, out LocalEntry))
			{
				Entry = LocalEntry;
				return true;
			}

			foreach (ObjectPackIndex Pack in PackIndexes)
			{
				if (Pack.TryGetEntry(Hash, out LocalEntry))
				{
					Entry = LocalEntry;
					return true;
				}
			}

			Entry = null;
			return false;
		}

		/// <summary>
		/// Flush any pending blobs to disk
		/// </summary>
		public async Task FlushAsync()
		{
			// Find the live set of objects
			HashSet<IoHash> LiveSet = new HashSet<IoHash>();
			foreach(IoHash RootHash in RootSet)
			{
				FindLiveSet(RootHash, LiveSet);
			}

			// Find the total cost of all the current blobs, then loop through the blobs trying to find a more optimal arrangement
			double TotalCost = PackIndexes.Sum(x => GetCostHeuristic(x)) + GetCostHeuristic(NextPackSize, TimeSpan.Zero);
			for (; ; )
			{
				// Exclude any objects that are in the pending blobs, since we will always upload these
				HashSet<IoHash> NewLiveSet = new HashSet<IoHash>(LiveSet);
				NewLiveSet.ExceptWith(NextPackHashToEntry.Values.Select(x => x.Hash));

				// Get the size and cost of the next blob
				double NextBlobCost = GetCostHeuristic(NextPackSize, TimeSpan.Zero);

				// Pass through all the blobs to find the best one to merge in
				double MergeCost = TotalCost;
				ObjectPackIndex? MergePack = null;
				for (int Idx = PackIndexes.Count - 1; Idx >= 0; Idx--)
				{
					ObjectPackIndex PackIndex = PackIndexes[Idx];

					// Try to merge any old blobs with the next blob
					if (PackIndex.Time < Time)
					{
						// Calculate the cost of the last blob if we merge this one with it. We remove blobs as we iterate
						// through the list, since subsequent blobs will not usefully contribute the same items.
						double NewTotalCost = TotalCost - GetCostHeuristic(PackIndex) - NextBlobCost;

						int NewNextPackSize = NextPackSize;
						foreach (ObjectPackEntry Entry in PackIndex.Blobs)
						{
							if (LiveSet.Contains(Entry.Hash))
							{
								if (NewNextPackSize + Entry.Length > MaxPackSize)
								{
									NewTotalCost += GetCostHeuristic(NewNextPackSize, TimeSpan.Zero);
									NewNextPackSize = 0;
								}
								NewNextPackSize += Entry.Length;
							}
						}

						NewTotalCost += GetCostHeuristic(NewNextPackSize, TimeSpan.Zero);

						// Compute the potential cost if we replace the partial blob with the useful parts of this blob
						if (NewTotalCost < MergeCost)
						{
							MergePack = PackIndex;
							MergeCost = NewTotalCost;
						}
					}

					// Remove any items in this blob from the remaining live set. No other blobs need to include them.
					NewLiveSet.ExceptWith(PackIndex.Blobs.Select(x => x.Hash));
				}

				// Bail out if we didn't find anything to merge
				if (MergePack == null)
				{
					break;
				}

				// Get the data for this blob
				ReadOnlyMemory<byte> MergeData = await BlobCollection.ReadBytesAsync(NamespaceId, MergePack.DataHash);

				// Add anything that's still part of the live set into the new blobs
				int Offset = 0;
				foreach (ObjectPackEntry Blob in MergePack.Blobs)
				{
					if (LiveSet.Contains(Blob.Hash))
					{
						ReadOnlyMemory<byte> Data = MergeData.Slice(Offset, Blob.Length);
						Add(Blob.Hash, Data.Span, Blob.Refs);
					}
					Offset += Blob.Length;
				}

				// Discard the old blob
				PackIndexes.Remove(MergePack);
				TotalCost = MergeCost;
			}

			// Write the current blob
			FlushCurrentPack();

			// Wait for all the writes to finish
			await Task.WhenAll(WriteTasks);
			WriteTasks.Clear();
		}

		/// <summary>
		/// Finds the live set for a particular tree, and updates tree entries with the size of used items within them
		/// </summary>
		/// <param name="Hash"></param>
		/// <param name="LiveSet"></param>
		void FindLiveSet(IoHash Hash, HashSet<IoHash> LiveSet)
		{
			if (LiveSet.Add(Hash))
			{
				ObjectPackEntry? Entry;
				if (!TryGetEntry(Hash, out Entry))
				{
					throw new Exception($"Missing blob {Hash} from working set");
				}
				foreach (IoHash Ref in Entry.Refs)
				{
					FindLiveSet(Ref, LiveSet);
				}
			}
		}

		/// <summary>
		/// Creates enough space to store the given block of data
		/// </summary>
		/// <param name="Size"></param>
		void CreateSpace(int Size)
		{
			// Get the last blob and make sure there's enough space in it
			if (NextPackSize + Size > MaxPackSize)
			{
				FlushCurrentPack();
			}

			// Resize the next blob buffer if necessary
			if (Size > NextPackData.Length)
			{
				Array.Resize(ref NextPackData, Size);
			}
		}

		/// <summary>
		/// Finalize the current blob and start writing it to storage
		/// </summary>
		void FlushCurrentPack()
		{
			if (NextPackSize > 0)
			{
				// Write the buffer to storage
				Array.Resize(ref NextPackData, NextPackSize);
				ReadOnlyMemory<byte> Data = NextPackData;
				IoHash DataHash = IoHash.Compute(Data.Span);
				WriteTasks.Add(Task.Run(() => BlobCollection.WriteBytesAsync(NamespaceId, DataHash, Data)));

				// Create the new index
				ObjectPackIndex Index = new ObjectPackIndex(Time, NextPackEntries.ToArray(), DataHash, NextPackSize);
				PackIndexes.Add(Index);

				// Clear the next pack buffer
				Reset();
			}
		}

		/// <inheritdoc cref="GetCostHeuristic(int, TimeSpan)"/>
		/// <param name="Index">Index to calculate the heuristic for</param>
		public double GetCostHeuristic(ObjectPackIndex Index) => GetCostHeuristic(Index.Blobs.Length, Time - Index.Time);

		/// <summary>
		/// Heuristic which estimates the cost of a particular blob. This is used to compare scenarios of merging blobs to reduce download
		/// size against keeping older blobs which a lot of agents already have.
		/// </summary>
		/// <param name="Size">Size of the blob</param>
		/// <param name="Age">Age of the blob</param>
		/// <returns>Heuristic for the cost of a blob</returns>
		public static double GetCostHeuristic(int Size, TimeSpan Age)
		{
			// Time overhead to starting a download
			const double DownloadInit = 0.1;

			// Download speed for agents, in bytes/sec
			const double DownloadRate = 1024 * 1024;

			// Probability of an agent having to download everything. Prevents bias against keeping a large number of files.
			const double CleanSyncProbability = 0.2;

			// Average length of time between agents having to update
			TimeSpan AverageCoherence = TimeSpan.FromHours(4.0);

			// Scale the age into a -1.0 -> 1.0 range around AverageCoherence
			double ScaledAge = (AverageCoherence - Age).TotalSeconds / AverageCoherence.TotalSeconds;

			// Get the probability of agents having to sync this blob based on its age. This is modeled as a logistic function (1 / (1 + e^-x))
			// with value of 0.5 at AverageCoherence, and MaxInterval at zero.

			// Find the scale factor for the 95% interval
			//    1 / (1 + e^-x) = MaxInterval
			//    e^-x = (1 / MaxInterval) - 1
			//    x = -ln((1 / MaxInterval) - 1)
			const double MaxInterval = 0.95;
			double SigmoidScale = -Math.Log((1.0 / MaxInterval) - 1.0);

			// Find the probability of having to sync this 
			double Param = ScaledAge * SigmoidScale;
			double Probability = 1.0 / (1.0 + Math.Exp(-Param));

			// Scale the probability against having to do a full sync
			Probability = CleanSyncProbability + (Probability * (1.0 - CleanSyncProbability));

			// Compute the final cost estimate; the amount of time we expect agents to spend downloading the file
			return Probability * (DownloadInit + (Size / DownloadRate));
		}
	}
}
