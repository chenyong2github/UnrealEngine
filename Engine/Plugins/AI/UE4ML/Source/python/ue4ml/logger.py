# Copyright Epic Games, Inc. All Rights Reserved.

from gym import logger


set_level = logger.set_level
DEBUG = logger.DEBUG
INFO = logger.INFO
WARN = logger.WARN
ERROR = logger.ERROR
DISABLED = logger.DISABLED


def debug(msg, *args):
    logger.debug('ue4ml: ' + msg, *args)

def info(msg, *args):
    logger.info('ue4ml: ' + msg, *args)

def warn(msg, *args):
    logger.warn('ue4ml: ' + msg, *args)

def error(msg, *args):
    logger.error('ue4ml: ' + msg, *args)

def get_level():
    return logger.MIN_LEVEL
