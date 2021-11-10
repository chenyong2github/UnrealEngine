# Copyright Epic Games, Inc. All Rights Reserved.

import subprocess

from typing import List, Optional

from switchboard.config import CONFIG
from switchboard.switchboard_logging import LOGGER

class InsightsLauncher(object):
    ''' Manages launching UnrealInsights '''

    def exe_path(self):
        ''' Returns the expected executable path. Extension not included '''
        
        return CONFIG.engine_exe_path(CONFIG.ENGINE_DIR.get_value(), "UnrealInsights")

    def launch(self, args:Optional[List[str]] = []):
        ''' Launches this application with the given arguments '''

        args.insert(0, self.exe_path())

        LOGGER.debug(f"Launching '{' '.join(args)}' ...")

        subprocess.Popen(args)
