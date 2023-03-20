// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base implementation for <see cref="IComputeBuffer"/>, which offers reader and writer objects that redirect to the owner
	/// </summary>
	public abstract class ComputeBuffer : IComputeBuffer
	{
		class ReaderImpl : IComputeBufferReader
		{
			readonly ComputeBuffer _owner;

			public ReaderImpl(ComputeBuffer owner) => _owner = owner;

			/// <inheritdoc/>
			public bool IsComplete => _owner.FinishedReading();

			/// <inheritdoc/>
			public void Advance(int size) => _owner.AdvanceReadPosition(size);

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetMemory() => _owner.GetReadMemory();

			/// <inheritdoc/>
			public ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken) => _owner.WaitAsync(currentLength, cancellationToken);
		}

		class WriterImpl : IComputeBufferWriter
		{
			readonly ComputeBuffer _owner;

			public WriterImpl(ComputeBuffer owner) => _owner = owner;

			/// <inheritdoc/>
			public void MarkComplete() => _owner.FinishWriting();

			/// <inheritdoc/>
			public void Advance(int size) => _owner.AdvanceWritePosition(size);

			/// <inheritdoc/>
			public Memory<byte> GetMemory() => _owner.GetWriteMemory();

			/// <inheritdoc/>
			public ValueTask FlushAsync(CancellationToken cancellationToken) => _owner.FlushWritesAsync(cancellationToken);
		}

		/// <inheritdoc/>
		public IComputeBufferReader Reader { get; }

		/// <inheritdoc/>
		public IComputeBufferWriter Writer { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		protected ComputeBuffer()
		{
			Reader = new ReaderImpl(this);
			Writer = new WriterImpl(this);
		}

		#region Reader

		/// <inheritdoc cref="IComputeBufferReader.IsComplete"/>
		protected abstract bool FinishedReading();

		/// <inheritdoc cref="IComputeBufferReader.Advance(Int32)"/>
		protected abstract void AdvanceReadPosition(int size);

		/// <inheritdoc cref="IComputeBufferReader.GetMemory"/>
		protected abstract ReadOnlyMemory<byte> GetReadMemory();

		/// <inheritdoc cref="IComputeBufferReader.WaitAsync(Int32, CancellationToken)"/>
		protected abstract ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken);

		#endregion
		#region Writer

		/// <inheritdoc cref="IComputeBufferWriter.MarkComplete"/>
		protected abstract void FinishWriting();

		/// <inheritdoc cref="IComputeBufferWriter.Advance(Int32)"/>
		protected abstract void AdvanceWritePosition(int size);

		/// <inheritdoc cref="IComputeBufferWriter.GetMemory()"/>
		protected abstract Memory<byte> GetWriteMemory();

		/// <inheritdoc cref="IComputeBufferWriter.FlushAsync(CancellationToken)"/>
		protected abstract ValueTask FlushWritesAsync(CancellationToken cancellationToken);

		#endregion
	}
}
