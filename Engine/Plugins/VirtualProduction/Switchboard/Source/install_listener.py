# Copyright Epic Games, Inc. All Rights Reserved.
import argparse
import sys

if sys.platform == 'win32':
    import winreg

    LISTENER_REG_KEY_NAME = "SwitchboardListener"
    RUN_KEY_PATH = r"Software\Microsoft\Windows\CurrentVersion\Run"

    def get_run_key():
        registry = winreg.ConnectRegistry(None, winreg.HKEY_CURRENT_USER)
        return winreg.OpenKey(registry, RUN_KEY_PATH, 0, winreg.KEY_ALL_ACCESS)


def install(autostart_command):
    if sys.platform == 'win32':
        run_key = get_run_key()
        winreg.SetValueEx(run_key, LISTENER_REG_KEY_NAME, 0, winreg.REG_SZ, autostart_command)
        winreg.CloseKey(run_key)
        print(f"Autostart command was installed to {RUN_KEY_PATH}\\{LISTENER_REG_KEY_NAME}")

def uninstall():
    if sys.platform == 'win32':
        run_key = get_run_key()
        winreg.DeleteValue(run_key, LISTENER_REG_KEY_NAME)
        winreg.CloseKey(run_key)
        print(f"Autostart command was removed from {RUN_KEY_PATH}\\{LISTENER_REG_KEY_NAME}")

def check_if_installed():
    if sys.platform == 'win32':
        run_key = get_run_key()
        try:
            winreg.QueryValueEx(run_key, LISTENER_REG_KEY_NAME)
        except WindowsError:
            print(f"Autostart command is not installed")
            return False
        else:
            print(f"Autostart command is installed to {RUN_KEY_PATH}\\{LISTENER_REG_KEY_NAME}")
            return True
        finally:
            winreg.CloseKey(run_key)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(help='Set up a command to be run automatically at system start', dest='action')
    subparsers.required = True

    install_parser = subparsers.add_parser('install')
    install_parser.add_argument('command', help='Command to install for autostart. Hint: Enclose the command in single quotation marks and escape double quotation marks.')

    uninstall_parser = subparsers.add_parser('uninstall')
    check_parser = subparsers.add_parser('check', help='Checks if command was already set up for autostart')

    options = parser.parse_args()
    if options.action == 'install':
        print(options.command)
        install(options.command)
    elif options.action == 'uninstall':
        uninstall()
    elif options.action == 'check':
        check_if_installed()
