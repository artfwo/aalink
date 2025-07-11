======
aalink
======

aalink is a Python wrapper for Ableton Link built for interactive applications
using asyncio event loops.

It provides a simple programming interface for writing concurrent Python code
synchronized to a beat. The beat can optionally be time-aligned with other
peers in an Ableton Link session.

Installation
============

aalink requires at least Python 3.9. It can be installed using pip::

    pip3 install aalink

It may be required to install the latest version of MSVC Runtime libraries
on Windows to use the binary wheels currently hosted on PyPI.

Usage
=====

aalink uses asyncio. To connect to a Link session, create a ``Link`` object,
passing the asyncio event loop to the constructor, and await for
``Link.sync()`` as follows:

.. code-block:: python

    import asyncio

    from aalink import Link

    async def main():
        loop = asyncio.get_running_loop()

        link = Link(120, loop)
        link.enabled = True

        while True:
            await link.sync(1)
            print('bang!')

    asyncio.run(main())

``Link.sync(n)`` returns a ``Future`` scheduled to be *done* when Link time
reaches next n-th beat on the timeline.

In the above example, awaiting for ``link.sync(1)`` will pause and resume
the ``main`` coroutine at beats 1, 2, 3, and so on.

Keep in mind that awaiting for ``sync(n)`` does not cause a coroutine to sleep
for the given number of beats. Regardless of the moment when the coroutine is
suspended, it will resume when the next closest n-th beat is reached on the
shared Link timeline, e.g. awaiting for ``sync(2)`` at beat 11.5 will resume
at beat 12.

Non-integral beat syncing is supported. For example:

.. code-block:: python

    await link.sync(1/2) # resumes at beats 0.5, 1, 1.5...
    await link.sync(3/2) # resumes at beats 1.5, 3, 4.5...

Sync events can be scheduled with an offset (also expressed in beats) by
passing an ``offset`` argument to ``sync()``. Use this to add groove to the
coroutine rhythm.

.. code-block:: python

    async def arpeggiate():
        for i in range(16):
            swing = 0.25 if i % 2 == 1 else 0

            await link.sync(1/2, offset=swing)
            print('###', i)

            await link.sync(1/2, offset=0)
            print('@@@', i)

Combine synced coroutines to run in series or concurrently:

.. code-block:: python

    import asyncio
    from aalink import Link

    async def main():
        loop = asyncio.get_running_loop()

        link = Link(120, loop)
        link.enabled = True

        async def sequence(name):
            for i in range(4):
                await link.sync(1)
                print('bang!', name)

        await sequence('a')
        await sequence('b')

        await asyncio.gather(sequence('c'), sequence('d'))

    asyncio.run(main())

Limitations
-----------

aalink aims to be punctual, but it is not 100% accurate due to the processing
delay in the internal scheduler and the uncertainty of event loop iterations
timing.

For convenience, the numerical values of futures returned from ``sync()``
aren't equal to the exact beat time from the moment the futures are *done*.
They correspond to the previously estimated resume times instead.

.. code-block:: python

    b = await link.sync(1) # b will be 1.0, returned at beat 1.00190
    b = await link.sync(1) # b will be 2.0, returned at beat 2.00027
    b = await link.sync(1) # b will be 3.0, returned at beat 3.00005

License
-------

Copyright (c) 2023 Artem Popov <art@artfwo.net>

aalink is licensed under the GNU General Public License (GPL) version 3.
You can find the full text of the GPL license in the ``LICENSE`` file included
in this repository.

aalink includes code from pybind11 and Ableton Link.

`pybind11 <https://pybind11.readthedocs.io/>`_

Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

`pybind11 license <https://github.com/pybind/pybind11/blob/master/LICENSE>`_

`Ableton Link <https://ableton.github.io/link/>`_

Copyright 2016, Ableton AG, Berlin. All rights reserved.

`Ableton Link license <https://github.com/Ableton/link/blob/master/LICENSE.md>`_
