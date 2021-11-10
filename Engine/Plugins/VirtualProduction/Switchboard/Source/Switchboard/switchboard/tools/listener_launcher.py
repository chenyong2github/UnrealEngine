# Copyright Epic Games, Inc. All Rights Reserved.

import subprocess

from typing import List, Optional

from switchboard import switchboard_utils as sb_utils

from switchboard.config import CONFIG
from switchboard.switchboard_logging import LOGGER

class ListenerLauncher(object):
    ''' Manages launching Switchboard Listener '''

    def exe_path(self):
        ''' Returns the expected executable path. Extension not included '''

        return CONFIG.listener_path()

    def launch(self, args:Optional[List[str]] = []):
        ''' Launches this application with the given arguments '''

        args.insert(0, self.exe_path())
        cmdline = ' '.join(args)

        LOGGER.debug(f"Launching '{cmdline}' ...")

        subprocess.Popen(cmdline)
