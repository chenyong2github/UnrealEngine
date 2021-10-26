// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Core;
using HordeCommon.Rpc;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Stream that takes in rpc chunks from an upload and copies them?
	/// </summary>
	public class ArtifactChunkStream : Stream
	{
		/// <summary>
		/// The grpc client reader. Should probably be templatized
		/// </summary>
		IAsyncStreamReader<UploadArtifactRequest> Reader;

		/// <summary>
		/// Position within the stream
		/// </summary>
		long StreamPosition;

		/// <summary>
		/// The length of the stream
		/// </summary>
		long StreamLength;

		/// <summary>
		/// The current request being read from
		/// </summary>
		UploadArtifactRequest? Request;

		/// <summary>
		/// Position within the current request
		/// </summary>
		int RequestPos;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Reader">the grpc reader</param>
		/// <param name="Length">filesize reported by the client</param>
		public ArtifactChunkStream(IAsyncStreamReader<UploadArtifactRequest> Reader, long Length)
		{
			this.Reader = Reader;
			this.StreamLength = Length;
		}

		/// <inheritdoc/>
		public override long Position
		{
			get { return StreamPosition; }
			set { throw new NotSupportedException(); }
		}

		/// <inheritdoc/>
		public override long Length => StreamLength;

		/// <inheritdoc/>
		public override void SetLength(long value) => throw new NotSupportedException();

		/// <inheritdoc/>
		public override bool CanSeek => false;

		/// <inheritdoc/>
		public override long Seek(long Offset, SeekOrigin Origin) => throw new NotSupportedException();

		/// <inheritdoc/>
		public override bool CanWrite => false;

		/// <inheritdoc/>
		public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

		/// <inheritdoc/>
		public override bool CanRead => true;

		/// <inheritdoc/>
		public override int Read(byte[] Buffer, int Offset, int Count)
		{
			return ReadAsync(Buffer, Offset, Count, CancellationToken.None).Result;
		}

		/// <inheritdoc/>
		public override async Task<int> ReadAsync(byte[] Buffer, int Offset, int Count, CancellationToken CancellationToken)
		{
			int BytesRead = 0;
			while (BytesRead < Count && StreamPosition < StreamLength)
			{
				if (Request == null || RequestPos == Request.Data.Length)
				{
					// Read the next request object
					if (!await Reader.MoveNext())
					{
						throw new EndOfStreamException("Unexpected end of stream while reading artifact");
					}

					Request = Reader.Current;
					RequestPos = 0;
				}
				else
				{
					// Copy data from the current request object
					int NumBytesToCopy = Math.Min(Count - BytesRead, Request.Data.Length - RequestPos);
					Request.Data.Span.Slice(RequestPos, NumBytesToCopy).CopyTo(Buffer.AsSpan(BytesRead, NumBytesToCopy));
					RequestPos += NumBytesToCopy;
					BytesRead += NumBytesToCopy;
					StreamPosition += NumBytesToCopy;
				}
			}
			return BytesRead;
		}

		/// <inheritdoc/>
		public override void Flush()
		{
		}
	}
}
