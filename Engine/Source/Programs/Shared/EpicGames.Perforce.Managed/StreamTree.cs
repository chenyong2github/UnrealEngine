// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Metadata for a Perforce file
	/// </summary>
	[DebuggerDisplay("{Path}")]
	public class StreamFile
	{
		/// <summary>
		/// Depot path for this file
		/// </summary>
		public Utf8String Path { get; }

		/// <summary>
		/// Length of the file, as reported by the server (actual size on disk may be different due to workspace options).
		/// </summary>
		public long Length { get; }

		/// <summary>
		/// Unique identifier for the file content
		/// </summary>
		public FileContentId ContentId { get; }

		/// <summary>
		/// Revision number of the file
		/// </summary>
		public int Revision { get; }

		#region Field names
		static Utf8String PathField = "path";
		static Utf8String LengthField = "len";
		static Utf8String DigestField = "dig";
		static Utf8String TypeField = "type";
		static Utf8String RevisionField = "rev";
		#endregion

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamFile(Utf8String Path, long Length, FileContentId ContentId, int Revision)
		{
			this.Path = Path;
			this.Length = Length;
			this.ContentId = ContentId;
			this.Revision = Revision;
		}

		/// <summary>
		/// Parse from a compact binary object
		/// </summary>
		/// <param name="Field"></param>
		/// <param name="DefaultPath">Default path for this file</param>
		/// <returns></returns>
		public StreamFile(CbObject Field, Utf8String DefaultPath)
		{
			Path = Field[PathField].AsString(DefaultPath);
			Length = Field[LengthField].AsInt64();

			Md5Hash Digest = new Md5Hash(Field[DigestField].AsBinary());
			Utf8String Type = Field[TypeField].AsString();
			ContentId = new FileContentId(Digest, Type);

			Revision = Field[RevisionField].AsInt32();
		}

		/// <summary>
		/// Write this object to compact binary
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="DefaultPath"></param>
		public void Write(CbWriter Writer, Utf8String DefaultPath)
		{
			if(Path != DefaultPath)
			{
				Writer.WriteString(PathField, Path);
			}
			Writer.WriteInteger(LengthField, Length);
			Writer.WriteBinary(DigestField, ContentId.Digest.Span);
			Writer.WriteString(TypeField, ContentId.Type);
			Writer.WriteInteger(RevisionField, Revision);
		}
	}

	/// <summary>
	/// Stores a reference to another tree
	/// </summary>
	public class StreamTreeRef
	{
		/// <summary>
		/// Base depot path for the directory
		/// </summary>
		public Utf8String Path { get; set; }

		/// <summary>
		/// Hash of the tree
		/// </summary>
		public IoHash Hash { get; set; }

		#region Field names
		static Utf8String PathField = "path";
		static Utf8String HashField = "hash";
		#endregion

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Path"></param>
		/// <param name="Hash"></param>
		public StreamTreeRef(Utf8String Path, IoHash Hash)
		{
			this.Path = Path;
			this.Hash = Hash;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Field"></param>
		/// <param name="DefaultPath"></param>
		public StreamTreeRef(CbObject Field, Utf8String DefaultPath)
		{
			Path = Field[PathField].AsString(DefaultPath);
			Hash = Field[HashField].AsObjectAttachment();
		}

		/// <summary>
		/// Gets the hash of this reference
		/// </summary>
		/// <returns></returns>
		public IoHash GetHash()
		{
			CbWriter Writer = new CbWriter();
			Writer.BeginObject();
			Write(Writer, Utf8String.Empty);
			Writer.EndObject();
			return Writer.ToObject().GetHash();
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="DefaultPath"></param>
		public void Write(CbWriter Writer, Utf8String DefaultPath)
		{
			if (Path != DefaultPath)
			{
				Writer.WriteString(PathField, Path);
			}
			Writer.WriteObjectAttachment(HashField, Hash);
		}
	}

	/// <summary>
	/// Information about a directory within a stream
	/// </summary>
	public class StreamTree
	{
		/// <summary>
		/// Map of name to file within the directory
		/// </summary>
		public Dictionary<Utf8String, StreamFile> NameToFile { get; } = new Dictionary<Utf8String, StreamFile>();

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<Utf8String, StreamTreeRef> NameToTree { get; } = new Dictionary<Utf8String, StreamTreeRef>(FileUtils.PlatformPathComparerUtf8);

		#region Field names
		static Utf8String NameField = "name";
		static Utf8String FilesField = "files";
		static Utf8String TreesField = "trees";
		#endregion

		/// <summary>
		/// Default constructor
		/// </summary>
		public StreamTree()
		{
		}

		/// <summary>
		/// Default constructor
		/// </summary>
		public StreamTree(Dictionary<Utf8String, StreamFile> NameToFile, Dictionary<Utf8String, StreamTreeRef> NameToTree)
		{
			this.NameToFile = NameToFile;
			this.NameToTree = NameToTree;
		}

		/// <summary>
		/// Deserialize a tree from a compact binary object
		/// </summary>
		/// <param name="Object"></param>
		/// <param name="BasePath"></param>
		public StreamTree(CbObject Object, Utf8String BasePath)
		{
			CbArray FileArray = Object[FilesField].AsArray();
			foreach (CbField FileField in FileArray)
			{
				CbObject FileObject = FileField.AsObject();

				Utf8String Name = FileObject[NameField].AsString();
				StreamFile File = new StreamFile(FileObject, GetDefaultPath(BasePath, Name));

				NameToFile.Add(Name, File);
			}

			CbArray TreeArray = Object[TreesField].AsArray();
			foreach (CbField TreeField in TreeArray)
			{
				CbObject TreeObject = TreeField.AsObject();

				Utf8String Name = TreeObject[NameField].AsString();
				StreamTreeRef Tree = new StreamTreeRef(TreeObject, GetDefaultPath(BasePath, Name));

				NameToTree.Add(Name, Tree);
			}
		}

		/// <summary>
		/// Finds the most common base path for items in this tree
		/// </summary>
		/// <returns>The most common base path</returns>
		public Utf8String FindBasePath()
		{
			Dictionary<Utf8String, int> BasePathToCount = new Dictionary<Utf8String, int>();
			foreach ((Utf8String Name, StreamFile File) in NameToFile)
			{
				AddBasePath(BasePathToCount, File.Path, Name);
			}
			foreach ((Utf8String Name, StreamTreeRef Tree) in NameToTree)
			{
				AddBasePath(BasePathToCount, Tree.Path, Name);
			}
			return (BasePathToCount.Count == 0) ? Utf8String.Empty : BasePathToCount.MaxBy(x => x.Value).Key;
		}

		/// <summary>
		/// Adds the base path of the given item to the count of similar items
		/// </summary>
		/// <param name="BasePathToCount"></param>
		/// <param name="Path"></param>
		/// <param name="Name"></param>
		static void AddBasePath(Dictionary<Utf8String, int> BasePathToCount, Utf8String Path, Utf8String Name)
		{
			if (Path.EndsWith(Name) && Path[^(Name.Length + 1)] == '/')
			{
				Utf8String BasePath = Path[..^(Name.Length + 1)];
				BasePathToCount.TryGetValue(BasePath, out int Count);
				BasePathToCount[BasePath] = Count + 1;
			}
		}

		/// <summary>
		/// Gets the default path for a child path
		/// </summary>
		/// <param name="BasePath"></param>
		/// <param name="Name"></param>
		/// <returns></returns>
		static Utf8String GetDefaultPath(Utf8String BasePath, Utf8String Name)
		{
			byte[] Data = new byte[BasePath.Length + 1 + Name.Length];
			BasePath.Span.CopyTo(Data);
			Data[BasePath.Length] = (byte)'/';
			Name.Span.CopyTo(Data.AsSpan(BasePath.Length + 1));
			return new Utf8String(Data);
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="BasePath"></param>
		public void Write(CbWriter Writer, Utf8String BasePath)
		{
			if (BasePath.EndsWith("/"))
			{
				throw new ArgumentException("BasePath must not end in a slash", nameof(BasePath));
			}

			if (NameToFile.Count > 0)
			{
				Writer.BeginArray(FilesField);
				foreach ((Utf8String Name, StreamFile File) in NameToFile.OrderBy(x => x.Key))
				{
					Writer.BeginObject();
					Writer.WriteString(NameField, Name);
					File.Write(Writer, BasePath);
					Writer.EndObject();
				}
				Writer.EndArray();
			}

			if (NameToTree.Count > 0)
			{
				Writer.BeginArray(TreesField);
				foreach ((Utf8String Name, StreamTreeRef Tree) in NameToTree.OrderBy(x => x.Key))
				{
					Writer.BeginObject();
					Writer.WriteString(NameField, Name);
					Tree.Write(Writer, BasePath);
					Writer.EndObject();
				}
				Writer.EndArray();
			}
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="BasePath"></param>
		/// <returns></returns>
		public CbObject ToCbObject(Utf8String BasePath)
		{
			if (BasePath.EndsWith("/"))
			{
				throw new ArgumentException("BasePath must not end in a slash", nameof(BasePath));
			}

			CbWriter Writer = new CbWriter();
			Writer.BeginObject();
			Write(Writer, BasePath);
			Writer.EndObject();
			return Writer.ToObject();
		}
	}
}
