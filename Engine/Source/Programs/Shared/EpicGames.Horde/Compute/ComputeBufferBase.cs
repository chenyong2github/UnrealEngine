// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base implementation for <see cref="IComputeBuffer"/>, which offers reader and writer objects that redirect to the owner
	/// </summary>
	public abstract class ComputeBufferBase : IComputeBuffer
	{
		class ReaderImpl : IComputeBufferReader
		{
			readonly ComputeBufferBase _buffer;

			/// <inheritdoc/>
			public IComputeBuffer Buffer => _buffer;

			public ReaderImpl(ComputeBufferBase outer) => _buffer = outer;

			/// <inheritdoc/>
			public void Dispose()
			{
				Dispose(true);
				GC.SuppressFinalize(this);
			}

			/// <summary>
			/// 
			/// </summary>
			/// <param name="disposing"></param>
			protected virtual void Dispose(bool disposing)
			{
			}

			/// <inheritdoc/>
			public bool IsComplete => _buffer.FinishedReading();

			/// <inheritdoc/>
			public void Advance(int size) => _buffer.AdvanceReadPosition(size);

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetMemory() => _buffer.GetReadMemory();

			/// <inheritdoc/>
			public ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken) => _buffer.WaitForDataAsync(currentLength, cancellationToken);
		}

		class WriterImpl : IComputeBufferWriter
		{
			readonly ComputeBufferBase _buffer;

			/// <inheritdoc/>
			public IComputeBuffer Buffer => _buffer;

			public WriterImpl(ComputeBufferBase buffer) => _buffer = buffer;

			/// <inheritdoc/>
			public void Dispose()
			{
				Dispose(true);
				GC.SuppressFinalize(this);
			}

			/// <summary>
			/// 
			/// </summary>
			/// <param name="disposing"></param>
			protected virtual void Dispose(bool disposing)
			{
				_buffer.FinishWriting();
			}

			/// <inheritdoc/>
			public void MarkComplete() => _buffer.FinishWriting();

			/// <inheritdoc/>
			public void Advance(int size) => _buffer.AdvanceWritePosition(size);

			/// <inheritdoc/>
			public Memory<byte> GetMemory() => _buffer.GetWriteMemory();

			/// <inheritdoc/>
			public ValueTask FlushAsync(CancellationToken cancellationToken) => _buffer.FlushAsync(cancellationToken);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <inheritdoc/>
		public abstract long Length { get; }

		/// <inheritdoc/>
		public IComputeBufferReader Reader { get; }

		/// <inheritdoc/>
		public IComputeBufferWriter Writer { get; }

		/// <summary>
		/// Default constructor
		/// </summary>
		protected ComputeBufferBase()
		{
			Reader = new ReaderImpl(this);
			Writer = new WriterImpl(this);
		}

		/// <summary>
		/// Standard dispose pattern
		/// </summary>
		protected virtual void Dispose(bool disposing)
		{
		}

		/// <inheritdoc/>
		public abstract Memory<byte> GetMemory(long offset, int length);

		#region Reader

		/// <inheritdoc cref="IComputeBufferReader.IsComplete"/>
		public abstract bool FinishedReading();

		/// <inheritdoc cref="IComputeBufferReader.Advance(Int32)"/>
		public abstract void AdvanceReadPosition(int size);

		/// <inheritdoc cref="IComputeBufferReader.GetMemory"/>
		public abstract ReadOnlyMemory<byte> GetReadMemory();

		/// <inheritdoc cref="IComputeBufferReader.WaitForDataAsync(Int32, CancellationToken)"/>
		public abstract ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken);

		#endregion
		#region Writer

		/// <inheritdoc cref="IComputeBufferWriter.MarkComplete"/>
		public abstract void FinishWriting();

		/// <inheritdoc cref="IComputeBufferWriter.Advance(Int32)"/>
		public abstract void AdvanceWritePosition(int size);

		/// <inheritdoc cref="IComputeBufferWriter.GetMemory()"/>
		public abstract Memory<byte> GetWriteMemory();

		/// <inheritdoc cref="IComputeBufferWriter.FlushAsync(CancellationToken)"/>
		public abstract ValueTask FlushAsync(CancellationToken cancellationToken);

		#endregion
	}
}
