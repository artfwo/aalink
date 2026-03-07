#! /usr/bin/env python3

import asyncio
from aalink import Link

async def bang(link, name, interval):
    while True:
        beat = await link.sync(interval)
        print('bang', name, beat)

async def main():
    link = Link(120)
    link.enabled = True

    asyncio.create_task(bang(link, 'a', 1))
    asyncio.create_task(bang(link, 'b', 2))

    await asyncio.Event().wait()

asyncio.run(main())
