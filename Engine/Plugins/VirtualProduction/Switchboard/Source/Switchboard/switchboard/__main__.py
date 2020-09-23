# Copyright Epic Games, Inc. All Rights Reserved.
from .switchboard_dialog import SwitchboardDialog
from .switchboard_logging import LOGGER

import signal
import sys

from PySide2 import QtCore, QtWidgets

# Build resources
# "C:\Program Files (x86)\Python37-32\Lib\site-packages\PySide2\pyside2-rcc" -o D:\Switchboard\switchboard\resources.py D:\Switchboard\switchboard\ui\resources.qrc


def launch():
    """
    Main for running standalone or in another application.
    """
    if sys.platform == 'win32':
        # works around some windows quirks so we can show the window icon
        import ctypes
        app_id = u'epicgames.virtualproduction.switchboard.0.1'
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(app_id)

    QtWidgets.QApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
    app = QtWidgets.QApplication(sys.argv)
    main_window = SwitchboardDialog()

    if not main_window.window:
        return

    # closure so we can access main_window and app
    def sigint_handler(*args):
        LOGGER.info("Received SIGINT, exiting...")
        main_window.on_exit()
        app.quit()

    # install handler for SIGINT so it's possible to exit the app when pressing ctrl+c in the terminal.
    signal.signal(signal.SIGINT, sigint_handler)

    main_window.window.show()

    # Enable file logging.
    LOGGER.enable_file_logging()

    # Logging start.
    LOGGER.info('----==== Switchboard ====----')

    # this will pump the event loop every 200ms so we can react faster on a SIGINT.
    # otherwise it will take several seconds before sigint_handler is called.
    timer = QtCore.QTimer()
    timer.start(200)
    timer.timeout.connect(lambda: None)

    sys.exit(app.exec_())


if __name__ == "__main__":
    launch()