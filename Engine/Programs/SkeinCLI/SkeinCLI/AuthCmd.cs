using McMaster.Extensions.CommandLineUtils;
using Serilog;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace SkeinCLI
{
    [Command(Name = "auth", Description = "Executes authorization related commands")]
    [Subcommand(typeof(AuthLoginCmd),
        typeof(AuthLogoutCmd))]
    class AuthCmd : SkeinCmdBase
    {
        private SkeinCmdBase Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("auth");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            //'skein auth' is an incomplete command, so we show help here even if --help is not specified
            app.ShowHelp();
            return base.OnExecute(app);
        }
    }

    [Command(Name = "login", Description = "Skein login")]
    class AuthLoginCmd : SkeinCmdBase
    {
        private AuthCmd Parent { get; set; }

        [Option("-u|--username", CommandOptionType.SingleValue, Description = "User Name")]
        public string UserName { get; set; }

        [Option("-p|--password", CommandOptionType.SingleValue, Description = "Password")]
        public string Password { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("login");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            if (string.IsNullOrEmpty(UserName) && !Quiet)
            {
                UserName = Prompt.GetString("User Name:",
                    promptColor: ConsoleColor.Black,
                    promptBgColor: ConsoleColor.White);
            }

            if (string.IsNullOrEmpty(Password) && !Quiet)
            {
                Password = Prompt.GetPassword("Password:",
                    promptColor: ConsoleColor.Black,
                    promptBgColor: ConsoleColor.White);
            }

            if (!string.IsNullOrEmpty(UserName) && !string.IsNullOrEmpty(Password))
            {
                Log.Logger.ForContext<ProjectsSnapshotsGetCmd>().Information($"User Name: {UserName}, Password: {Password}");
                Log.Logger.ForContext<ProjectsListCmd>().Information("Login process here [...]");
            }
            else 
            {
                Log.Logger.ForContext<ProjectsListCmd>().Information("Login cancelled");
            }

            
            return base.OnExecute(app);
        }
    }

    [Command(Name = "logout", Description = "Skein logout")]
    class AuthLogoutCmd : SkeinCmdBase
    {
        private AuthCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("logout");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            Log.Logger.ForContext<ProjectsListCmd>().Information("Logout process here [...]");
            return base.OnExecute(app);
        }
    }
}