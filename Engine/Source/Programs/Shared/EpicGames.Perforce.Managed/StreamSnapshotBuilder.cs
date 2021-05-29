// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Utility class to build a stream snapshot in memory
	/// </summary>
	public class StreamSnapshotBuilder
	{
		/// <summary>
		/// Map of name to file within the directory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, StreamFileInfo> NameToFile = new Dictionary<ReadOnlyUtf8String, StreamFileInfo>();

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, StreamSnapshotBuilder> NameToSubDirectory = new Dictionary<ReadOnlyUtf8String, StreamSnapshotBuilder>(FileUtils.PlatformPathComparerUtf8);

		/// <summary>
		/// Default constructor
		/// </summary>
		public StreamSnapshotBuilder()
		{
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Snapshot"></param>
		public StreamSnapshotBuilder(StreamSnapshot Snapshot)
			: this(Snapshot, Snapshot.Root)
		{
		}

		/// <summary>
		/// Construct a stream snapshot from the given subtree
		/// </summary>
		/// <param name="Snapshot"></param>
		/// <param name="Hash"></param>
		public StreamSnapshotBuilder(StreamSnapshot Snapshot, Digest<Sha1> Hash)
		{
			StreamDirectoryInfo OldDirectory = Snapshot.Lookup(Hash);
			foreach ((ReadOnlyUtf8String Name, StreamFileInfo File) in OldDirectory.NameToFile)
			{
				NameToFile[Name] = File;
			}
			foreach ((ReadOnlyUtf8String Name, Digest<Sha1> SubDirHash) in OldDirectory.NameToSubDirectory)
			{
				NameToSubDirectory[Name] = new StreamSnapshotBuilder(Snapshot, SubDirHash);
			}
		}

		/// <summary>
		/// Encode this subtree
		/// </summary>
		/// <param name="HashToDirectory"></param>
		/// <returns></returns>
		public Digest<Sha1> Encode(Dictionary<Digest<Sha1>, StreamDirectoryInfo> HashToDirectory)
		{
			// Create the stream directory object
			StreamDirectoryInfo Directory = new StreamDirectoryInfo();
			Directory.NameToFile = NameToFile;
			foreach ((ReadOnlyUtf8String Name, StreamSnapshotBuilder SubDirBuilder) in NameToSubDirectory)
			{
				Directory.NameToSubDirectory[Name] = SubDirBuilder.Encode(HashToDirectory);
			}

			// Serialize it
			byte[] Data = new byte[Directory.GetSerializedSize()];

			MemoryWriter Writer = new MemoryWriter(Data);
			Writer.WriteStreamDirectoryInfo(Directory);
			Writer.CheckOffset(Data.Length);

			Digest<Sha1> Hash = Digest.Compute<Sha1>(Data);
			HashToDirectory[Hash] = Directory;

			return Hash;
		}
	}
}
