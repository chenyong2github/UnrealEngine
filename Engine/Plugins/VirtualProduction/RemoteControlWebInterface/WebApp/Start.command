#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Simple wrapper around Start.sh using the
# .command extension enables it to be run from the OSX Finder.

sh "`dirname "$0"`"/Start.sh $*
