// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using ICSharpCode.SharpZipLib.BZip2;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension methods for compression
	/// </summary>
	static class CompressionExtensions
	{
		/// <summary>
		/// Compress a block of data with bzip2
		/// </summary>
		/// <returns>The compressed data</returns>
		public static byte[] CompressBzip2(this ReadOnlyMemory<byte> Memory)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("CompressBzip2").StartActive();
			Scope.Span.SetTag("DecompressedSize", Memory.Length.ToString(CultureInfo.InvariantCulture));

			byte[] CompressedData;
			using (MemoryStream Stream = new MemoryStream())
			{
				byte[] DecompressedSize = new byte[4];
				BinaryPrimitives.WriteInt32LittleEndian(DecompressedSize.AsSpan(), Memory.Length);
				Stream.Write(DecompressedSize.AsSpan());

				using (BZip2OutputStream CompressedStream = new BZip2OutputStream(Stream))
				{
					CompressedStream.Write(Memory.Span);
				}

				CompressedData = Stream.ToArray();
			}

			Scope.Span.SetTag("CompressedSize", CompressedData.Length.ToString(CultureInfo.InvariantCulture));
			return CompressedData;
		}

		/// <summary>
		/// Decompress the data
		/// </summary>
		/// <returns>The decompressed data</returns>
		public static byte[] DecompressBzip2(this ReadOnlyMemory<byte> Memory)
		{
			int DecompressedSize = BinaryPrimitives.ReadInt32LittleEndian(Memory.Span);

			using IScope Scope = GlobalTracer.Instance.BuildSpan("DecompressBzip2").StartActive();
			Scope.Span.SetTag("CompressedSize", Memory.Length.ToString(CultureInfo.InvariantCulture));
			Scope.Span.SetTag("DecompressedSize", DecompressedSize.ToString(CultureInfo.InvariantCulture));

			byte[] Data = new byte[DecompressedSize];
			using (ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Memory.Slice(4)))
			{
				using (BZip2InputStream DecompressedStream = new BZip2InputStream(Stream))
				{
					int ReadSize = DecompressedStream.Read(Data.AsSpan());
					if (ReadSize != Data.Length)
					{
						throw new InvalidDataException($"Compressed data is too short (expected {Data.Length} bytes, got {ReadSize} bytes)");
					}
				}
			}
			return Data;
		}
	}
}
