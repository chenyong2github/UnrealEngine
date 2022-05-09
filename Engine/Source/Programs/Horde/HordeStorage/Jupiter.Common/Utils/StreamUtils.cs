// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Threading.Tasks;

namespace Jupiter.Utils
{
    public static class StreamUtils
    {
        public static async Task<byte[]> ToByteArray(this Stream s)
        {
            await using MemoryStream ms = new MemoryStream();
            await s.CopyToAsync(ms);
            return ms.ToArray();
        }
    }
}
