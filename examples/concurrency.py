#! /usr/bin/env python3

import asyncio
from aalink import Link

async def bang(link, name, interval):
    while True:
        beat = await link.sync(interval)
        print('bang', name, beat)

async def main():
    loop = asyncio.get_running_loop()

    link = Link(120, loop)
    link.enabled = True

    asyncio.create_task(bang(link, 'a', 1))
    asyncio.create_task(bang(link, 'b', 2))

    await loop.create_future()

asyncio.run(main())
