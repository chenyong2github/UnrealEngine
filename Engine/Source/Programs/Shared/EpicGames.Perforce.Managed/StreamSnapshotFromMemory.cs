// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
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
	/// Stores the contents of a stream in memory
	/// </summary>
	public class StreamSnapshotFromMemory : StreamSnapshot
	{
		/// <summary>
		/// The current signature for saved directory objects
		/// </summary>
		static readonly byte[] CurrentSignature = { (byte)'W', (byte)'S', (byte)'D', 5 };

		/// <summary>
		/// The root digest
		/// </summary>
		public override StreamTreeRef Root { get; }

		/// <summary>
		/// Map of digest to directory
		/// </summary>
		public IReadOnlyDictionary<IoHash, CbObject> HashToTree { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Root"></param>
		/// <param name="HashToTree"></param>
		public StreamSnapshotFromMemory(StreamTreeRef Root, Dictionary<IoHash, CbObject> HashToTree)
		{
			this.Root = Root;
			this.HashToTree = HashToTree;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Builder"></param>
		public StreamSnapshotFromMemory(StreamTreeBuilder Builder)
		{
			Dictionary<IoHash, CbObject> HashToTree = new Dictionary<IoHash, CbObject>();
			this.Root = Builder.EncodeRef(Tree => EncodeObject(Tree, HashToTree));
			this.HashToTree = HashToTree;
		}

		/// <summary>
		/// Serialize to a compact binary object
		/// </summary>
		/// <param name="BasePath"></param>
		/// <returns></returns>
		static IoHash EncodeObject(StreamTree Tree, Dictionary<IoHash, CbObject> HashToTree)
		{
			CbObject Object = Tree.ToCbObject();

			IoHash Hash = Object.GetHash();
			HashToTree[Hash] = Object;

			return Hash;
		}

		/// <inheritdoc/>
		public override StreamTree Lookup(StreamTreeRef Ref)
		{
			return new StreamTree(Ref.Path, HashToTree[Ref.Hash]);
		}

		/// <summary>
		/// Load a stream directory from a file on disk
		/// </summary>
		/// <param name="InputFile">File to read from</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>New StreamDirectoryInfo object</returns>
		public static async Task<StreamSnapshotFromMemory?> TryLoadAsync(FileReference InputFile, Utf8String BasePath, CancellationToken CancellationToken)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(InputFile);
			if (!Data.AsSpan().StartsWith(CurrentSignature))
			{
				return null;
			}

			CbObject RootObj = new CbObject(Data.AsMemory(CurrentSignature.Length));

			CbObject RootObj2 = RootObj["root"].AsObject();
			Utf8String RootPath = RootObj2["path"].AsString(BasePath);
			StreamTreeRef Root = new StreamTreeRef(RootPath, RootObj2);

			CbArray Array = RootObj["items"].AsArray();

			Dictionary<IoHash, CbObject> HashToTree = new Dictionary<IoHash, CbObject>(Array.Count);
			foreach (CbField Element in Array)
			{
				CbObject ObjectElement = Element.AsObject();
				IoHash Hash = ObjectElement["hash"].AsHash();
				CbObject Tree = ObjectElement["tree"].AsObject();
				HashToTree[Hash] = Tree;
			}

			return new StreamSnapshotFromMemory(Root, HashToTree);
		}

		/// <summary>
		/// Saves the contents of this object to disk
		/// </summary>
		/// <param name="OutputFile">The output file to write to</param>
		public async Task Save(FileReference OutputFile, Utf8String BasePath)
		{
			CbWriter Writer = new CbWriter();
			Writer.BeginObject();
						
			Writer.BeginObject("root");
			if (Root.Path != BasePath)
			{
				Writer.WriteString("path", Root.Path);
			}
			Root.Write(Writer);
			Writer.EndObject();
			
			Writer.BeginArray("items");
			foreach ((IoHash Hash, CbObject Tree) in HashToTree)
			{
				Writer.BeginObject();
				Writer.WriteHash("hash", Hash);
				Writer.WriteObject("tree", Tree);
				Writer.EndObject();
			}
			Writer.EndArray();

			Writer.EndObject();

			byte[] Data = Writer.ToByteArray();
			using (FileStream OutputStream = FileReference.Open(OutputFile, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await OutputStream.WriteAsync(CurrentSignature, 0, CurrentSignature.Length);
				await OutputStream.WriteAsync(Data, 0, Data.Length);
			}
		}
	}
}
