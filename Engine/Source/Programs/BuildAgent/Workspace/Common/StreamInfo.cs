// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using Tools.DotNETCommon;
using Tools.DotNETCommon.Perforce;

namespace BuildAgent.Workspace.Common
{
	/// <summary>
	/// Represents a file within a stream
	/// </summary>
	class StreamFileInfo
	{
		/// <summary>
		/// Name of this file
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Length of the file, as reported by the server (actual size on disk may be different due to workspace options).
		/// </summary>
		public readonly long Length;

		/// <summary>
		/// Content id for this file
		/// </summary>
		public readonly FileContentId ContentId;

		/// <summary>
		/// The parent directory
		/// </summary>
		public readonly StreamDirectoryInfo Directory;

		/// <summary>
		/// The depot file and revision that need to be synced for this file
		/// </summary>
		public readonly string DepotFileAndRevision;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this file</param>
		/// <param name="Length">Length of the file on the server</param>
		/// <param name="ContentId">Content id for this file</param>
		/// <param name="Directory">The parent directory</param>
		/// <param name="DepotFileAndRevision">The depot file and revision that need to be synced for this file</param>
		public StreamFileInfo(string Name, long Length, FileContentId ContentId, StreamDirectoryInfo Directory, string DepotFileAndRevision)
		{
			this.Name = Name;
			this.Length = Length;
			this.ContentId = ContentId;
			this.Directory = Directory;
			this.DepotFileAndRevision = DepotFileAndRevision;
		}

		/// <summary>
		/// Constructor for reading a file info from disk
		/// </summary>
		/// <param name="Reader">Binary reader to read data from</param>
		/// <param name="Directory">Parent directory</param>
		public StreamFileInfo(BinaryReader Reader, StreamDirectoryInfo Directory)
		{
			this.Name = Reader.ReadString();
			this.Length = Reader.ReadInt64();
			this.ContentId = new FileContentId(Reader);
			this.Directory = Directory;
			this.DepotFileAndRevision = Reader.ReadString();
		}

		/// <summary>
		/// Save the file info to disk
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		public void Write(BinaryWriter Writer)
		{
			Writer.Write(Name);
			Writer.Write(Length);
			ContentId.Write(Writer);
			Writer.Write(DepotFileAndRevision);
		}

		/// <summary>
		/// Get the path to this file relative to the root of the stream
		/// </summary>
		/// <returns>Relative path to the file</returns>
		public string GetRelativePath()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendPath(Builder);
			Builder.Append(Name);
			return Builder.ToString();
		}

		/// <summary>
		/// Format the path to the file for the debugger
		/// </summary>
		/// <returns>Path to the file</returns>
		public override string ToString()
		{
			return GetRelativePath();
		}
	}

	/// <summary>
	/// Information about a directory within a stream
	/// </summary>
	class StreamDirectoryInfo
	{
		/// <summary>
		/// The current signature for saved directory objects
		/// </summary>
		static readonly byte[] CurrentSignature = { (byte)'W', (byte)'S', (byte)'D', 1 };

		/// <summary>
		/// The directory name
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// The parent directory
		/// </summary>
		public readonly StreamDirectoryInfo ParentDirectory;

		/// <summary>
		/// Map of name to file within the directory
		/// </summary>
		public Dictionary<string, StreamFileInfo> NameToFile = new Dictionary<string, StreamFileInfo>(StringComparer.Ordinal);

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<string, StreamDirectoryInfo> NameToSubDirectory = new Dictionary<string, StreamDirectoryInfo>(FileUtils.PlatformPathComparer);

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamDirectoryInfo()
			: this("", null)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the directory</param>
		/// <param name="ParentDirectory">Parent directory</param>
		public StreamDirectoryInfo(string Name, StreamDirectoryInfo ParentDirectory)
		{
			this.Name = Name;
			this.ParentDirectory = ParentDirectory;
		}

		/// <summary>
		/// Constructor for reading from disk
		/// </summary>
		/// <param name="Reader">Reader to read data from</param>
		/// <param name="ParentDirectory">The parent directory to initialize this directory with</param>
		public StreamDirectoryInfo(BinaryReader Reader, StreamDirectoryInfo ParentDirectory)
		{
			this.Name = Reader.ReadString();
			this.ParentDirectory = ParentDirectory;

			int NumFiles = Reader.ReadInt32();
			for(int Idx = 0; Idx < NumFiles; Idx++)
			{
				StreamFileInfo File = new StreamFileInfo(Reader, this);
				NameToFile.Add(File.Name, File);
			}

			int NumDirectories = Reader.ReadInt32();
			for(int Idx = 0; Idx < NumDirectories; Idx++)
			{
				StreamDirectoryInfo SubDirectory = new StreamDirectoryInfo(Reader, this);
				NameToSubDirectory.Add(SubDirectory.Name, SubDirectory);
			}
		}

		/// <summary>
		/// Writes the contents of this stream to disk
		/// </summary>
		/// <param name="Writer">Writer to serialize to</param>
		public void Write(BinaryWriter Writer)
		{
			Writer.Write(Name);

			Writer.Write(NameToFile.Count);
			foreach(StreamFileInfo File in NameToFile.Values)
			{
				File.Write(Writer);
			}

			Writer.Write(NameToSubDirectory.Count);
			foreach(StreamDirectoryInfo SubDirectory in NameToSubDirectory.Values)
			{
				SubDirectory.Write(Writer);
			}
		}

		/// <summary>
		/// Load a stream directory from a file on disk
		/// </summary>
		/// <param name="InputFile">File to read from</param>
		/// <returns>New StreamDirectoryInfo object</returns>
		public static StreamDirectoryInfo Load(FileReference InputFile)
		{
			using(FileStream Stream = File.Open(InputFile.FullName, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				byte[] Signature = new byte[CurrentSignature.Length];
				if(Stream.Read(Signature, 0, CurrentSignature.Length) != CurrentSignature.Length)
				{
					throw new InvalidDataException(String.Format("Unable to read signature bytes from {0}", InputFile));
				}
				if(!Enumerable.SequenceEqual(Signature, CurrentSignature))
				{
					throw new InvalidDataException(String.Format("Cached stream contents at {0} has incorrect signature", InputFile));
				}

				using(GZipStream CompressedStream = new GZipStream(Stream, CompressionMode.Decompress))
				{
					using(BinaryReader Reader = new BinaryReader(CompressedStream, Encoding.UTF8, true))
					{
						return new StreamDirectoryInfo(Reader, null);
					}
				}
			}
		}

		/// <summary>
		/// Saves the contents of this object to disk
		/// </summary>
		/// <param name="OutputFile">The output file to write to</param>
		public void Save(FileReference OutputFile)
		{
			using(FileStream Stream = File.Open(OutputFile.FullName, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
			{
				Stream.Write(CurrentSignature, 0, CurrentSignature.Length);

				using(GZipStream CompressedStream = new GZipStream(Stream, CompressionMode.Compress))
				{
					using(BinaryWriter Writer = new BinaryWriter(CompressedStream, Encoding.UTF8, true))
					{
						Write(Writer);
					}
				}
			}
		}

		/// <summary>
		/// Get all the files in this directory
		/// </summary>
		/// <returns>List of files</returns>
		public List<StreamFileInfo> GetFiles()
		{
			List<StreamFileInfo> Files = new List<StreamFileInfo>();
			AppendFiles(Files);
			return Files;
		}

		/// <summary>
		/// Append the contents of this directory and subdirectories to a list
		/// </summary>
		/// <param name="Files">List to append to</param>
		public void AppendFiles(List<StreamFileInfo> Files)
		{
			foreach(StreamDirectoryInfo SubDirectory in NameToSubDirectory.Values)
			{
				SubDirectory.AppendFiles(Files);
			}
			Files.AddRange(NameToFile.Values);
		}

		/// <summary>
		/// Gets the relative path to this directory, with a trailing slash
		/// </summary>
		/// <returns>Relative path to this directory</returns>
		public string GetRelativePath()
		{
			StringBuilder Builder = new StringBuilder();
			AppendPath(Builder);
			return Builder.ToString();
		}

		/// <summary>
		/// Append the relative path to this directory to the given string builder
		/// </summary>
		/// <param name="Builder">String builder to append to</param>
		public void AppendPath(StringBuilder Builder)
		{
			if(ParentDirectory != null)
			{
				ParentDirectory.AppendPath(Builder);
				Builder.Append(Name);
			}
			Builder.Append('/');
		}

		/// <summary>
		/// Format the path to the directory for debugging
		/// </summary>
		/// <returns>Path to the directory</returns>
		public override string ToString()
		{
			return GetRelativePath();
		}
	}
}
