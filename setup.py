import platform

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup

__version__ = "0.0.4"

ext = Pybind11Extension(
    "aalink",
    sources=[
        "src/aalink.cpp"
    ],
    define_macros=[("VERSION_INFO", __version__)],
    include_dirs=[
        "link/include",
        "link/modules/asio-standalone/asio/include",
    ],
)

if platform.system() == 'Linux':
    ext.extra_compile_args += ["-DLINK_PLATFORM_LINUX"]
    ext.extra_compile_args += ["-Wno-multichar"]
elif platform.system() == 'Darwin':
    ext.extra_compile_args += ["-DLINK_PLATFORM_MACOSX"]
    ext.extra_compile_args += ["-Wno-multichar"]
elif platform.system() == 'Windows':
    ext.extra_compile_args += ["-DLINK_PLATFORM_WINDOWS"]
else:
    raise RuntimeError('Unsupported platform: {}'.format(platform.system()))

setup(
    name="aalink",
    version=__version__,
    author="Artem Popov",
    author_email="art@artfwo.net",
    url="https://github.com/artfwo/aalink",
    description="Async Python interface for Ableton Link",
    long_description=open("README.rst").read(),
    ext_modules=[ext],
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.7",
    classifiers = [
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: GNU General Public License v3 or later (GPLv3+)",
        "Programming Language :: Python :: 3",
        "Topic :: Artistic Software",
        "Topic :: Multimedia :: Sound/Audio",
        "Topic :: Software Development :: Libraries",
        "Topic :: System :: Networking",
    ]
)
