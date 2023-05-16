#! /usr/bin/env python3

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
