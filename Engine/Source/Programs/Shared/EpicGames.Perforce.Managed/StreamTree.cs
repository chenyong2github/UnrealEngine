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
		/// <param name="Path">Path to the file</param>
		/// <param name="Field"></param>
		/// <returns></returns>
		public StreamFile(Utf8String Path, CbObject Field)
		{
			this.Path = Path;
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
		public void Write(CbWriter Writer)
		{
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
		/// <param name="Path"></param>
		/// <param name="Field"></param>
		public StreamTreeRef(Utf8String Path, CbObject Field)
		{
			this.Path = Path;
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
			Write(Writer);
			Writer.EndObject();
			return Writer.ToObject().GetHash();
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(CbWriter Writer)
		{
			Writer.WriteObjectAttachment(HashField, Hash);
		}
	}

	/// <summary>
	/// Information about a directory within a stream
	/// </summary>
	public class StreamTree
	{
		/// <summary>
		/// The path to this tree
		/// </summary>
		public Utf8String Path { get; }

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
		static Utf8String PathField = "path";
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
		public StreamTree(Utf8String Path, Dictionary<Utf8String, StreamFile> NameToFile, Dictionary<Utf8String, StreamTreeRef> NameToTree)
		{
			CheckPath(Path);
			this.Path = Path;

			this.NameToFile = NameToFile;
			this.NameToTree = NameToTree;
		}

		/// <summary>
		/// Deserialize a tree from a compact binary object
		/// </summary>
		public StreamTree(Utf8String Path, CbObject Object)
		{
			CheckPath(Path);
			this.Path = Path;

			CbArray FileArray = Object[FilesField].AsArray();
			foreach (CbField FileField in FileArray)
			{
				CbObject FileObject = FileField.AsObject();

				Utf8String Name = FileObject[NameField].AsString();
				Utf8String FilePath = ReadPath(FileObject, Path, Name);
				StreamFile File = new StreamFile(FilePath, FileObject);

				NameToFile.Add(Name, File);
			}

			CbArray TreeArray = Object[TreesField].AsArray();
			foreach (CbField TreeField in TreeArray)
			{
				CbObject TreeObject = TreeField.AsObject();

				Utf8String Name = TreeObject[NameField].AsString();
				Utf8String TreePath = ReadPath(TreeObject, Path, Name);
				StreamTreeRef Tree = new StreamTreeRef(TreePath, TreeObject);

				NameToTree.Add(Name, Tree);
			}
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(CbWriter Writer)
		{
			if (NameToFile.Count > 0)
			{
				Writer.BeginArray(FilesField);
				foreach ((Utf8String Name, StreamFile File) in NameToFile.OrderBy(x => x.Key))
				{
					Writer.BeginObject();
					Writer.WriteString(NameField, Name);
					WritePath(Writer, File.Path, Path, Name);
					File.Write(Writer);
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
					WritePath(Writer, Tree.Path, Path, Name);
					Tree.Write(Writer);
					Writer.EndObject();
				}
				Writer.EndArray();
			}
		}

		/// <summary>
		/// Reads a path from an object, defaulting it to the parent path plus the child name
		/// </summary>
		/// <param name="Object"></param>
		/// <param name="BasePath"></param>
		/// <param name="Name"></param>
		/// <returns></returns>
		static Utf8String ReadPath(CbObject Object, Utf8String BasePath, Utf8String Name)
		{
			Utf8String Path = Object[PathField].AsString();
			if (Path.IsEmpty)
			{
				byte[] Data = new byte[BasePath.Length + 1 + Name.Length];
				BasePath.Memory.CopyTo(Data);
				Data[BasePath.Length] = (byte)'/';
				Name.Memory.CopyTo(Data.AsMemory(BasePath.Length + 1));
				Path = new Utf8String(Data);
			}
			return Path;
		}

		/// <summary>
		/// Writes a path if it's not the default (the parent path, a slash, followed by the child name)
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Path"></param>
		/// <param name="ParentPath"></param>
		/// <param name="Name"></param>
		static void WritePath(CbWriter Writer, Utf8String Path, Utf8String ParentPath, Utf8String Name)
		{
			if (Path.Length != ParentPath.Length + Name.Length + 1 || !Path.StartsWith(ParentPath) || Path[ParentPath.Length] != '/' || !Path.EndsWith(Name))
			{
				Writer.WriteString(PathField, Path);
			}
		}

		/// <summary>
		/// Checks that a base path does not have a trailing slash
		/// </summary>
		/// <param name="Path"></param>
		/// <exception cref="ArgumentException"></exception>
		static void CheckPath(Utf8String Path)
		{
			if (Path.Length > 0 && Path[Path.Length - 1] == '/')
			{
				throw new ArgumentException("BasePath must not end in a slash", nameof(Path));
			}
		}

		/// <summary>
		/// Convert to a compact binary object
		/// </summary>
		/// <returns></returns>
		public CbObject ToCbObject()
		{
			CbWriter Writer = new CbWriter();
			Writer.BeginObject();
			Write(Writer);
			Writer.EndObject();
			return Writer.ToObject();
		}
	}
}
