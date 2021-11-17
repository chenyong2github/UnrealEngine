// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.IO;
using Blake3;
using Datadog.Trace;
using Force.Crc32;
using Jupiter;
using Jupiter.Utils;
using K4os.Compression.LZ4;

namespace Horde.Storage.Implementation
{
    public class CompressedBufferUtils
    {
        private readonly OodleCompressor _oodleCompressor;

        public CompressedBufferUtils(OodleCompressor oodleCompressor)
        {
            _oodleCompressor = oodleCompressor;
        }

        public class Header
        {
            public enum CompressionMethod : byte
            {
                // Header is followed by one uncompressed block. 
                None = 0,
                // Header is followed by an array of compressed block sizes then the compressed blocks. 
                Oodle=3,
                LZ4 = 4,
            }

            public static uint ExpectedMagic = 0xb7756362; // <dot>ucb
            public static uint HeaderLength = 64;

            // A magic number to identify a compressed buffer. Always 0xb7756362.
            public uint Magic;
            // A CRC-32 used to check integrity of the buffer. Uses the polynomial 0x04c11db7.
            public uint Crc32;
            // The method used to compress the buffer. Affects layout of data following the header. 
            public CompressionMethod Method;
            public byte CompressionLevel;
            public byte CompressionMethodUsed;

            // The power of two size of every uncompressed block except the last. Size is 1 << BlockSizeExponent. 
            public byte BlockSizeExponent;

            // The number of blocks that follow the header. 
            public uint BlockCount;

            // The total size of the uncompressed data. 
            public ulong TotalRawSize;

            // The total size of the compressed data including the header. 
            public ulong TotalCompressedSize;

            /** The hash of the uncompressed data. */
            public byte[] RawHash = Array.Empty<byte>();

            public void ByteSwap()
            {
                Magic = BinaryPrimitives.ReverseEndianness(Magic);
                Crc32 = BinaryPrimitives.ReverseEndianness(Crc32);
                BlockCount = BinaryPrimitives.ReverseEndianness(BlockCount);
                TotalRawSize = BinaryPrimitives.ReverseEndianness(TotalRawSize);
                TotalCompressedSize = BinaryPrimitives.ReverseEndianness(TotalCompressedSize);
            }
        }

        public Header ExtractHeader(byte[] content)
        {
            // the header is always stored big endian
            bool needsByteSwap = BitConverter.IsLittleEndian;
            if (content.Length < Header.HeaderLength)
                throw new ArgumentOutOfRangeException(nameof(content), $"Content was less then {Header.HeaderLength} bytes and thus is not a compressed buffer");

            using MemoryStream ms = new MemoryStream(content);
            using BinaryReader reader = new BinaryReader(ms);

            Header header = new Header
            {
                Magic = reader.ReadUInt32(),
                Crc32 = reader.ReadUInt32(),
                Method = (Header.CompressionMethod) reader.ReadByte(),
                CompressionLevel = reader.ReadByte(),
                CompressionMethodUsed = reader.ReadByte(),
                BlockSizeExponent = reader.ReadByte(),
                BlockCount = reader.ReadUInt32(),
                TotalRawSize = reader.ReadUInt64(),
                TotalCompressedSize = reader.ReadUInt64()
            };
            byte[] hash = reader.ReadBytes(32); // a full blake3 hash
            header.RawHash = hash;

            if (needsByteSwap)
                header.ByteSwap();

            if (ms.Position != Header.HeaderLength)
                throw new Exception($"Read {ms.Position} bytes but expected to read {Header.HeaderLength}");

            if (header.Magic != Header.ExpectedMagic)
                throw new InvalidMagicException(header.Magic, Header.ExpectedMagic);

            // calculate the crc from the start of the method field (skipping magic which is a constant and the crc field itself)
            const int methodOffset = sizeof(uint) + sizeof(uint);

            // none compressed objects have no extra blocks
            uint blocksByteUsed = header.Method != Header.CompressionMethod.None ? header.BlockCount * (uint)sizeof(uint) : 0;
            uint calculatedCrc = Crc32Algorithm.Compute(content, methodOffset, (int)(Header.HeaderLength - methodOffset + blocksByteUsed));
            
            if (header.Crc32 != calculatedCrc)
                throw new InvalidHashException(header.Crc32, calculatedCrc);

            return header;
        }


        public byte[] DecompressContent(byte[] content)
        {
            Header header = ExtractHeader(content);

            if (content.LongLength < (long)header.TotalCompressedSize)
                throw new Exception($"Expected buffer to be {header.TotalCompressedSize} but it was {content.LongLength}");

            ulong decompressedPayloadOffset = 0;
            byte[] decompressedPayload = new byte[header.TotalRawSize];

            ReadOnlySpan<byte> memory = new ReadOnlySpan<byte>(content);
            memory = memory.Slice((int)Header.HeaderLength);

            bool willHaveBlocks = header.Method != Header.CompressionMethod.None;
            if (willHaveBlocks)
            {
                uint[] compressedBlockSizes = new uint[header.BlockCount];
                for (int i = 0; i < header.BlockCount; i++)
                {
                    uint compressedBlockSize = BinaryPrimitives.ReadUInt32BigEndian(memory);
                    compressedBlockSizes[i] = compressedBlockSize;
                    memory = memory.Slice(sizeof(uint));
                }

                ulong blockSize = 1ul << header.BlockSizeExponent;
                ulong compressedOffset = 0;

                foreach (uint compressedBlockSize in compressedBlockSizes)
                {
                    ulong rawBlockSize = Math.Min(header.TotalRawSize - decompressedPayloadOffset, blockSize);
                    ReadOnlySpan<byte> compressedPayload = memory.Slice((int)compressedOffset, (int)compressedBlockSize);
                    Span<byte> targetSpan = new Span<byte>(decompressedPayload, (int)decompressedPayloadOffset, (int)rawBlockSize);

                    int writtenBytes;
                    // if a block has the same raw and compressed size its uncompressed and we should not attempt to decompress it
                    if (rawBlockSize == compressedBlockSize)
                    {
                        writtenBytes = (int)rawBlockSize;
                        compressedPayload.CopyTo(targetSpan);
                    }
                    else
                    {
                        writtenBytes = DecompressPayload(compressedPayload, header, rawBlockSize, targetSpan);
                    }

                    decompressedPayloadOffset += (uint)writtenBytes;
                    compressedOffset += compressedBlockSize;
                }
            }
            else
            {
                // if no compression is applied there are no extra blocks and just a single chunk that is uncompressed
                Span<byte> targetSpan = new Span<byte>(decompressedPayload, 0, (int)header.TotalRawSize);
                DecompressPayload(memory, header, header.TotalRawSize, targetSpan);
                decompressedPayloadOffset = header.TotalRawSize;
            }
           
            if (header.TotalRawSize != decompressedPayloadOffset)
                throw new Exception("Did not decompress the full payload");

            {
                using Scope _ = Tracer.Instance.StartActive("cb.hash");
                using Hasher hasher = Hasher.New();
                hasher.UpdateWithJoin(decompressedPayload);
                Hash blake3Hash = hasher.Finalize();

                Span<byte> hash = blake3Hash.AsSpan();
                if (!new ByteArrayComparer().Equals(hash.ToArray(), header.RawHash))
                    throw new Exception($"Payload was expected to be {StringUtils.FormatAsHexString(header.RawHash)} but was {StringUtils.FormatAsHexString(hash.ToArray())}");
            }


            return decompressedPayload;
        }

        private int DecompressPayload(ReadOnlySpan<byte> compressedPayload, Header header, ulong rawBlockSize, Span<byte> target)
        {
             switch (header.Method)
            {
                case Header.CompressionMethod.None:
                    compressedPayload.CopyTo(target);
                    return compressedPayload.Length;
                case Header.CompressionMethod.Oodle:
                {
                    // we have separate enums from the official oodle api to make sure they are stable enums so we need to translate to the ones expected by the api
                    OodleLZ_Compressor compressor = OodleUtils.ToOodleApiCompressor((OoodleCompressorMethod)header.CompressionMethodUsed);
                    OodleLZ_CompressionLevel compressionLevel = OodleUtils.ToOodleApiCompressionLevel((OoodleCompressionLevel)header.CompressionLevel);
                    long writtenBytes = _oodleCompressor.Decompress(compressor,compressedPayload.ToArray(), compressionLevel, (long)rawBlockSize, out byte[] result);
                    if (writtenBytes == 0)
                    {
                        throw new Exception("Failed to run oodle decompress");
                    }
                    result.CopyTo(target);
                    return (int)writtenBytes;
                }
                case Header.CompressionMethod.LZ4:
                {
                    int writtenBytes = LZ4Codec.Decode(compressedPayload, target);
                    return writtenBytes;
                }
                default:
                    throw new ArgumentOutOfRangeException(nameof(header.Method), header.Method, null);
            }
        }
    }

    internal class InvalidHashException : Exception
    {
        public InvalidHashException(uint headerCrc32, uint calculatedCrc) : base($"Header specified crc \"{headerCrc32}\" but calculated hash was \"{calculatedCrc}\"")
        {
            
        }
    }

    internal class InvalidMagicException : Exception
    {
        public InvalidMagicException(uint headerMagic, uint expectedMagic) : base($"Header magic \"{headerMagic}\" was incorrect, expected to be {expectedMagic}")
        {
        }
    }

    // from OodleDataCompression.h , we define our own enums for oodle compressions used and convert to the ones expected in the oodle api
    public enum OoodleCompressorMethod: byte
    {
        NotSet = 0,
        Selkie = 1,
        Mermaid = 2,
        Kraken  = 3,
        Leviathan = 4
    }

    public enum OoodleCompressionLevel : sbyte
    {
        HyperFast4 = -4,
        HyperFast3 = -3,
        HyperFast2 = -2,
        HyperFast1 = -1,
        None = 0,
        SuperFast = 1,
        VeryFast = 2,
        Fast = 3,
        Normal = 4,
        Optimal1 = 5,
        Optimal2 = 6,
        Optimal3 = 7,
        Optimal4 = 8,
    }

    public static class OodleUtils
    {
        public static OodleLZ_CompressionLevel ToOodleApiCompressionLevel(OoodleCompressionLevel compressionLevel)
        {
            switch (compressionLevel)
            {
                case OoodleCompressionLevel.HyperFast4:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_HyperFast4;
                case OoodleCompressionLevel.HyperFast3:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_HyperFast3;
                case OoodleCompressionLevel.HyperFast2:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_HyperFast2;
                case OoodleCompressionLevel.HyperFast1:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_HyperFast1;
                case OoodleCompressionLevel.None:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_None;
                case OoodleCompressionLevel.SuperFast:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_SuperFast;
                case OoodleCompressionLevel.VeryFast:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_VeryFast;
                case OoodleCompressionLevel.Fast:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Fast;
                case OoodleCompressionLevel.Normal:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Normal;
                case OoodleCompressionLevel.Optimal1:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Optimal1;
                case OoodleCompressionLevel.Optimal2:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Optimal2;
                case OoodleCompressionLevel.Optimal3:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Optimal3;
                case OoodleCompressionLevel.Optimal4:
                    return OodleLZ_CompressionLevel.OodleLZ_CompressionLevel_Optimal4;
                default:
                    throw new ArgumentOutOfRangeException(nameof(compressionLevel), compressionLevel, null);
            }
        }

        public static OodleLZ_Compressor ToOodleApiCompressor(OoodleCompressorMethod compressor)
        {
            switch (compressor)
            {
                case OoodleCompressorMethod.NotSet:
                    return OodleLZ_Compressor.OodleLZ_Compressor_None;
                case OoodleCompressorMethod.Selkie:
                    return OodleLZ_Compressor.OodleLZ_Compressor_Selkie;
                case OoodleCompressorMethod.Mermaid:
                    return OodleLZ_Compressor.OodleLZ_Compressor_Mermaid;
                case OoodleCompressorMethod.Kraken:
                    return OodleLZ_Compressor.OodleLZ_Compressor_Kraken;
                case OoodleCompressorMethod.Leviathan:
                    return OodleLZ_Compressor.OodleLZ_Compressor_Leviathan;
                default:
                    throw new ArgumentOutOfRangeException(nameof(compressor), compressor, null);
            }
        }

    }
}
