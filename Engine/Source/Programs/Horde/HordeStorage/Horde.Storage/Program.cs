// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO.Pipelines;
using System.Net.Sockets;
using Horde.Storage.Implementation.Kestrel;
using Jupiter;

namespace Horde.Storage
{
    // ReSharper disable once ClassNeverInstantiated.Global
    public class Program
    {
        public static int Main(string[] args)
        {
            return BaseProgram<HordeStorageStartup>.BaseMain(args, HttpConnectionFactory);
        }

        private static BaseHttpConnection HttpConnectionFactory(IServiceProvider sp, PipeReader reader, PipeWriter writer, Socket socket)
        {
            return new KestrelDispatcher(sp, reader, writer, socket);
        }
    }
}
