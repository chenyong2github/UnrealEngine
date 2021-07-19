// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Interface for a stream snapshot
	/// </summary>
	public abstract class StreamSnapshot
	{
		/// <summary>
		/// Empty snapshot instance
		/// </summary>
		public static StreamSnapshot Empty => new StreamSnapshotFromMemory(new StreamTreeBuilder());

		/// <summary>
		/// The root digest
		/// </summary>
		public abstract StreamTreeRef Root { get; }

		/// <summary>
		/// Lookup a directory by reference
		/// </summary>
		/// <param name="Ref">The reference</param>
		/// <returns></returns>
		public abstract StreamTree Lookup(StreamTreeRef Ref);
	}

	/// <summary>
	/// Extension methods for IStreamSnapshot
	/// </summary>
	static class StreamSnapshotExtensions
	{
		/// <summary>
		/// Get all the files in this directory
		/// </summary>
		/// <returns>List of files</returns>
		public static List<StreamFile> GetFiles(this StreamSnapshot Snapshot)
		{
			List<StreamFile> Files = new List<StreamFile>();
			AppendFiles(Snapshot, Snapshot.Root, Files);
			return Files;
		}

		/// <summary>
		/// Append the contents of this directory and subdirectories to a list
		/// </summary>
		/// <param name="Files">List to append to</param>
		static void AppendFiles(StreamSnapshot Snapshot, StreamTreeRef TreeRef, List<StreamFile> Files)
		{
			StreamTree DirectoryInfo = Snapshot.Lookup(TreeRef);
			foreach (StreamTreeRef SubDirRef in DirectoryInfo.NameToTree.Values)
			{
				AppendFiles(Snapshot, SubDirRef, Files);
			}
			Files.AddRange(DirectoryInfo.NameToFile.Values);
		}
	}
}
