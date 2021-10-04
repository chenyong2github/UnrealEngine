// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Configuration.Json;
using Serilog;
using System;
using System.IO;
using System.Threading.Tasks;

namespace SkeinCLI
{
    class Program
    {
        private static async Task<int> Main(string[] args)
        {
            var Configuration = new ConfigurationBuilder()
                .SetBasePath(Directory.GetCurrentDirectory())
                .AddJsonFile(AppDomain.CurrentDomain.BaseDirectory + "\\appsettings.json", optional: true, reloadOnChange: true)
                .AddEnvironmentVariables()
                .Build();

            Log.Logger = new LoggerConfiguration()
               .ReadFrom.Configuration(Configuration)
               .CreateLogger();

            /*using (var progress = new ProgressBar())
            {
                for (int i = 0; i <= 100; i++)
                {
                    progress.Report((double)i / 100);
                    System.Threading.Thread.Sleep(20);
                }
            }*/

            try
            {
                return await CommandLineApplication.ExecuteAsync<SkeinCmd>(args);
            }
            catch (Exception ex)
            {
                Log.ForContext<Program>().Error(ex, "Oh no!");
                return 1;
            }
            finally 
            {
                Log.CloseAndFlush();
            }
        }
    }
}
