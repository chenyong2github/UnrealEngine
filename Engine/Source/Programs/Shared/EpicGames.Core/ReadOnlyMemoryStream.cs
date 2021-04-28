// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Stream which reads from a <see cref="ReadOnlyMemory{byte}"/>
	/// </summary>
	public class ReadOnlyMemoryStream : Stream
	{
		/// <summary>
		/// The buffer to read from
		/// </summary>
		ReadOnlyMemory<byte> Memory;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">The memory to read from</param>
		public ReadOnlyMemoryStream(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
		}

		/// <inheritdoc/>
		public override bool CanRead => true;

		/// <inheritdoc/>
		public override bool CanSeek => true;

		/// <inheritdoc/>
		public override bool CanWrite => false;

		/// <inheritdoc/>
		public override long Length => Memory.Length;

		/// <inheritdoc/>
		public override long Position { get; set; }

		/// <inheritdoc/>
		public override void Flush()
		{
		}

		/// <inheritdoc/>
		public override int Read(byte[] Buffer, int Offset, int Count)
		{
			int CopyLength = Math.Min(Count, (int)(Memory.Length - Position));
			Memory.Slice((int)Position, CopyLength).CopyTo(Buffer.AsMemory(Offset, CopyLength));
			Position += CopyLength;
			return CopyLength;
		}

		/// <inheritdoc/>
		public override long Seek(long Offset, SeekOrigin Origin)
		{
			switch (Origin)
			{
				case SeekOrigin.Begin:
					Position = Offset;
					break;
				case SeekOrigin.Current:
					Position += Offset;
					break;
				case SeekOrigin.End:
					Position = Memory.Length + Offset;
					break;
				default:
					throw new ArgumentException(null, nameof(Origin));
			}
			return Position;
		}

		/// <inheritdoc/>
		public override void SetLength(long value) => throw new InvalidOperationException();

		/// <inheritdoc/>
		public override void Write(byte[] buffer, int offset, int count) => throw new InvalidOperationException();
	}
}
