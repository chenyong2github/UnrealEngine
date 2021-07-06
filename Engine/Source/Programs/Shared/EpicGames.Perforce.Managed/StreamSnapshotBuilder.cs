// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
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
		public Dictionary<Utf8String, StreamFile> NameToFile = new Dictionary<Utf8String, StreamFile>();

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<Utf8String, StreamSnapshotBuilder> NameToSubDirectory = new Dictionary<Utf8String, StreamSnapshotBuilder>(FileUtils.PlatformPathComparerUtf8);

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
		/// <param name="StreamTreeRef"></param>
		public StreamSnapshotBuilder(StreamSnapshot Snapshot, StreamTreeRef StreamTreeRef)
		{
			StreamTree OldDirectory = Snapshot.Lookup(StreamTreeRef);
			foreach ((Utf8String Name, StreamFile File) in OldDirectory.NameToFile)
			{
				NameToFile[Name] = File;
			}
			foreach ((Utf8String Name, StreamTreeRef SubDirRef) in OldDirectory.NameToTree)
			{
				NameToSubDirectory[Name] = new StreamSnapshotBuilder(Snapshot, SubDirRef);
			}
		}

		/// <summary>
		/// Encode this subtree
		/// </summary>
		/// <param name="HashToDirectory"></param>
		/// <returns></returns>
		public StreamTreeRef Encode(Dictionary<IoHash, CbObject> HashToDirectory)
		{
			// Create the stream directory object
			StreamTree Directory = new StreamTree();
			Directory.NameToFile = NameToFile;
			foreach ((Utf8String SubDirName, StreamSnapshotBuilder SubDirBuilder) in NameToSubDirectory)
			{
				Directory.NameToTree[SubDirName] = SubDirBuilder.Encode(HashToDirectory);
			}

			// Get the base directory
			Utf8String BasePath = Directory.FindBasePath();
			CbObject Object = Directory.ToCbObject(BasePath);
			IoHash Hash = Object.GetHash();
			HashToDirectory[Hash] = Object;

			return new StreamTreeRef(BasePath, Hash);
		}
	}
}
