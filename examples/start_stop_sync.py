#! /usr/bin/env python3

# This example demonstrates resetting local time
# when the transport start/stop state of Link changes.
#
# Try running the example with another peer, e.g. LinkHut,
# make sure that start/stop syncing is enabled and try
# starting and stopping the transport.

import asyncio
from aalink import Link

async def main():
    loop = asyncio.get_running_loop()

    link = Link(120, loop)
    link.enabled = True
    link.start_stop_sync_enabled = True
    link.quantum = 4

    def on_start_stop(playing):
        if playing:
            link.request_beat_at_start_playing_time(0)

    link.set_start_stop_callback(on_start_stop)

    while True:
        b = await link.sync(1)
        if link.playing:
            print('bang!', b)

asyncio.run(main())
