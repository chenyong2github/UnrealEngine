# Copyright Epic Games, Inc. All Rights Reserved.

from setuptools import setup, find_packages
import sys
import os.path

# Don't import ue4ml module here, since dependencies may not be installed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'ue4ml'))
from version import VERSION

module_name = 'ue4ml'
module_root = os.path.dirname(__file__)

sys.path.insert(0, os.path.join(module_root, module_name))

packages = [package for package in find_packages(module_root) if module_name in package]

setup(name='ue4ml',
      version=VERSION,
      description='Extension to OpenAI Gym adding capability to interface with UE4 projects using UE4ML plugin.',
      author='Mieszko Zielinski @ Epic Games',
      author_email='mieszko.zielinski@epicgames.com',
      license='',
      packages=packages,
      zip_safe=True,
      install_requires=['gym', 'msgpack-rpc-python', 'numpy'],
      python_requires='>=3.5.*',
)